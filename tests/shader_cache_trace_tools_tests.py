"""P0 report attribution tests: uncertainty must survive incomplete evidence."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/shaders"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/assets"))
import shader_cache_trace as trace


class ReportTests(unittest.TestCase):
    def report(self, lines):
        with tempfile.TemporaryDirectory() as folder:
            log, output = Path(folder) / "game.log", Path(folder) / "report.json"
            log.write_text("\n".join(lines), encoding="utf-8")
            trace.report_file(log, output)
            return json.loads(output.read_text(encoding="utf-8"))

    def test_temporal_overlap_is_not_claimed_as_cause(self):
        report = self.report([
            "shader_trace: connect_state panel=123 previous=18 requested=35 t_us=99 lr=123",
            "shader_trace: event=translation phase=active_song end_phase=active_song site=vulkan key=1 begin_us=100 end_us=200 duration_us=100 success=true",
            "shader_trace: event=gpu_frame phase=active_song end_phase=active_song site=swap key=0 begin_us=50 end_us=50050 duration_us=50000 success=true",
            "shader_trace: event=compile_wait phase=active_song end_phase=active_song site=wait key=0 begin_us=50050 end_us=50060 duration_us=10 success=true",
        ])
        frame = report["long_frames"][0]
        self.assertEqual(report["phases"], [])
        self.assertEqual([e["event"] for e in frame["overlapping_work"]], ["translation"])
        self.assertIn("causation unproven", frame["assessment"])

    def test_no_overlap_stays_unresolved_and_capped_details_are_visible(self):
        report = self.report([
            "shader_trace: summary snapshot=1 reason=periodic phase=active_song event=translation count=2048 suppressed=0",
            "shader_trace: summary snapshot=2 reason=periodic phase=active_song event=translation count=3000 suppressed=952",
            "shader_trace: event=guest_frame phase=active_song end_phase=active_song site=swap key=0 begin_us=1 end_us=300001 duration_us=300000 success=true",
        ])
        self.assertTrue(report["detail_truncated"])
        self.assertEqual(report["latest_cumulative_counters"]["active_song/translation"]["count"], "3000")
        self.assertIn("unresolved", report["long_frames"][0]["assessment"])

    def test_non_compile_gpu_operation_preserves_site_and_uncertainty(self):
        report = self.report([
            "shader_trace: event=gpu_operation phase=active_song end_phase=active_song site=d3d12.queue_execute key=42 begin_us=100 end_us=800100 duration_us=800000 success=true",
            "shader_trace: event=gpu_frame phase=active_song end_phase=active_song site=swap key=0 begin_us=50 end_us=800150 duration_us=800100 success=true",
        ])
        frame = report["long_frames"][0]
        self.assertEqual(frame["overlapping_work"][0]["event"], "gpu_operation")
        self.assertEqual(frame["overlapping_work"][0]["site"], "d3d12.queue_execute")
        self.assertIn("causation unproven", frame["assessment"])

    def test_run_names_cannot_escape_out(self):
        for name in ["../cache", "..", "D:/normal-cache", "", "a/b"]:
            with self.assertRaises(ValueError):
                trace.run_dir(name)

    def test_file_path_and_producer_thread_survive_quoted_site(self):
        source = 'cnt0001:\\songs\\Bounce Man\\a "quoted"\n.mogg'
        report = self.report([
            '[info] [core] [t123] shader_trace: event=file_read phase=active_song '
            f'end_phase=active_song site={json.dumps(source)} key=FF variant=100 '
            'begin_us=100 end_us=100100 duration_us=100000 success=true',
            '[info] [core] [t456] shader_trace: event=gpu_frame phase=active_song '
            'end_phase=active_song site="swap" key=0 begin_us=1 end_us=200001 '
            'duration_us=200000 success=true',
        ])
        frame = report['long_frames'][0]
        self.assertEqual(frame['thread_id'], 456)
        self.assertEqual(frame['overlapping_work'][0]['thread_id'], 123)
        self.assertEqual(frame['overlapping_work'][0]['site'], source)
        self.assertIn('causation unproven', frame['assessment'])

    def test_modular_log_prefix_preserves_producer_thread(self):
        result = self.report([
            '2026-11-18 04:49:24.352 I core       t22060 shader_trace: event=gpu_frame phase=menu end_phase=menu site="swap" key=0 begin_us=0 end_us=50000 duration_us=50000 success=true',
        ])
        self.assertEqual(result["long_frames"][0]["thread_id"], 22060)

    def test_partial_records_are_reported_without_losing_usable_evidence(self):
        report = self.report([
            'shader_trace: event=gpu_frame phase=active_song end_phase=active_song '
            'site=swap key=0 begin_us=1 end_us=100001 duration_us=100000 success=true',
            'shader_trace: event=file_read site="truncated path',
            'shader_trace: event=file_read phase=active_song begin_us=500',
        ])
        self.assertEqual(len(report['long_frames']), 1)
        self.assertEqual([r['line'] for r in report['damaged_trace_lines']], [2, 3])
        self.assertIn('unresolved', report['long_frames'][0]['assessment'])


class RuntimeInventoryTests(unittest.TestCase):
    def stage(self, build, platform="windows", cache=""):
        sys.path.insert(0, str(trace.ROOT / "installer/tools"))
        from runtime_inventory import runtime_targets, target_filename
        (build / "CMakeCache.txt").write_text(cache)
        names = [target_filename(name, platform) for name in runtime_targets(build, platform)]
        names.append("rb_blitz.exe" if platform == "windows" else "rb_blitz")
        for name in names:
            (build / name).write_bytes(name.encode())
        return names

    def test_modular_contract_and_additional_staged_dependencies_are_hashed(self):
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder)
            names = self.stage(build)
            (build / "vulkan").mkdir()
            (build / "vulkan/vulkan-1.dll").write_bytes(b"loader")
            (build / "custom-provider.dll").write_bytes(b"provider")
            (build / "sdk-build").mkdir()
            (build / "sdk-build/unstaged-test.dll").write_bytes(b"not shipped")
            before = trace.runtime_binary_inventory(build)
            self.assertEqual(set(before), set(names) | {"vulkan/vulkan-1.dll", "custom-provider.dll"})
            (build / "vulkan/vulkan-1.dll").write_bytes(b"different loader")
            after = trace.runtime_binary_inventory(build)
            self.assertNotEqual(before["vulkan/vulkan-1.dll"]["sha256"], after["vulkan/vulkan-1.dll"]["sha256"])
            self.assertEqual(after["custom-provider.dll"]["bytes"], 8)

    def test_configured_optional_backends_are_not_required(self):
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder)
            names = self.stage(build, cache="REXGLUE_USE_VULKAN:BOOL=OFF\nREXGLUE_ENABLE_TRACY:BOOL=OFF\n")
            self.assertNotIn("rexgpu-xenos-vulkan.dll", names)
            self.assertNotIn("TracyClient.dll", names)
            self.assertEqual(set(trace.runtime_binary_inventory(build)), set(names))

    def test_missing_configured_module_is_an_error(self):
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder)
            self.stage(build)
            (build / "rexxam.dll").unlink()
            with self.assertRaisesRegex(ValueError, "rexxam.dll"):
                trace.runtime_binary_inventory(build)

    def test_old_baseline_layout_remains_supported(self):
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder)
            names = ["rb_blitz.exe", "rexruntime.dll", "rexgpu-xenos.dll", "SDL3.dll"]
            for name in names:
                (build / name).write_bytes(name.encode())
            self.assertEqual(set(trace.runtime_binary_inventory(build)), set(names))

    def test_linux_loader_soname_and_macos_framework_binary_are_recorded(self):
        for platform, extra in [("linux", "vulkan/libvulkan.so.1"),
                                ("macos", "Frameworks/MoltenVK.framework/Versions/A/MoltenVK")]:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as folder:
                build = Path(folder)
                self.stage(build, platform)
                path = build / extra
                path.parent.mkdir(parents=True)
                path.write_bytes(b"native runtime")
                self.assertIn(extra, trace.runtime_binary_inventory(build))


class PreparationProvenanceTests(unittest.TestCase):
    def test_alternate_build_records_its_source_checkout_and_capture_tool_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source, build, user, game = [folder / name for name in ('baseline', 'build', 'user', 'game')]
            for path in (source / 'src', build, user, game): path.mkdir(parents=True)
            (source / 'src/changed.cpp').write_text('baseline source')
            (source / 'rexglue-sdk/include/rex').mkdir(parents=True)
            (source / 'rexglue-sdk/include/rex/new.h').write_text('new public SDK header')
            for name in ('rb_blitz.exe', 'rexruntime.dll', 'rexgpu-xenos.dll'):
                (build / name).write_bytes(name.encode())
            (game / 'default.xex').write_bytes(b'game fixture')
            (user / 'save.dat').write_bytes(b'fixed save')
            profile = folder / 'launcher.toml'
            profile.write_text('[profile]\n')
            def source_git(*args, cwd):
                self.assertEqual(cwd, source)
                if args == ('rev-parse', 'HEAD'): return 'baseline-project\n'
                if args == ('-C', 'rexglue-sdk', 'rev-parse', 'HEAD'): return 'baseline-sdk\n'
                if args == ('diff', 'HEAD'): return 'baseline project diff\n'
                if args == ('-C', 'rexglue-sdk', 'diff', 'HEAD'): return 'baseline SDK diff\n'
                if args == ('diff', '--name-only', 'HEAD'): return 'src/changed.cpp\n'
                if args[:2] == ('ls-files', '--others'): return ''
                if args[:4] == ('-C', 'rexglue-sdk', 'ls-files', '--others'): return 'include/rex/new.h\n'
                if args == ('-C', 'rexglue-sdk', 'submodule', 'status', '--recursive'): return 'vendor-pin\n'
                self.fail(args)
            args = SimpleNamespace(name='baseline-cold', source_root=source, build_dir=build,
                user_data=user, game=game, profile=profile, warm_from=None, backend='d3d12',
                variant='retail', route='one fixed song', game_arg=[])
            with patch.object(trace, 'RUNS', folder / 'runs'), patch.object(trace, 'git', side_effect=source_git):
                trace.prepare(args)
            run = folder / 'runs/baseline-cold'
            manifest = json.loads((run / 'manifest.json').read_text())
            self.assertEqual(manifest['project'], 'baseline-project')
            self.assertEqual(manifest['sdk'], 'baseline-sdk')
            self.assertEqual((run / 'inputs/sdk-source/include/rex/new.h').read_text(), 'new public SDK header')
            self.assertEqual((run / 'inputs/sdk-submodules.txt').read_text(), 'vendor-pin\n')
            self.assertEqual((run / 'inputs/source/src/changed.cpp').read_text(), 'baseline source')
            self.assertEqual((run / 'inputs/project.diff').read_text(), 'baseline project diff\n')
            self.assertEqual(manifest['capture_tools']['shader_cache_trace.py']['sha256'],
                             trace.digest(Path(trace.__file__)))

    def test_warm_cache_cannot_cross_binary_or_backend_identity(self):
        binaries = {'rb_blitz.exe': {'sha256': 'baseline', 'bytes': 1}}
        previous = dict(binaries=binaries, backend='d3d12', variant='retail', game_xex_sha256='game')
        trace.validate_warm_source(previous, binaries, 'd3d12', 'retail', 'game')
        for key, changed in [('binaries', {'rb_blitz.exe': {'sha256': 'upgrade', 'bytes': 1}}),
                             ('backend', 'vulkan'), ('variant', 'ultimate'), ('game_xex_sha256', 'other')]:
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'same binary bytes'):
                trace.validate_warm_source({**previous, key: changed}, binaries, 'd3d12', 'retail', 'game')


class CaptureArgumentsTests(unittest.TestCase):
    def arguments(self, extra):
        args = type("CaptureArgs", (), dict(game=Path("game"), backend="vulkan",
                    variant="ultimate", game_arg=extra))()
        return trace.capture_arguments(Path("rb_blitz.exe"), args, Path("run"), Path("run/cache"))

    def test_default_log_level_is_exactly_one_info_option(self):
        argv = self.arguments([])
        self.assertEqual([arg for arg in argv if arg.startswith("--log_level")], ["--log_level=info"])
        self.assertIn("--game_data_root=game", argv)
        self.assertIn("--ultimate_mode=1", argv)

    def test_explicit_level_replaces_default_without_changing_other_arguments(self):
        extra = ["--probe_framerate=true", "--log_level=debug", "--vsync=true"]
        argv = self.arguments(extra)
        self.assertEqual([arg for arg in argv if arg.startswith("--log_level")], ["--log_level=debug"])
        self.assertEqual(argv[-len(extra):], extra)
        self.assertIn("--game_data_root=game", argv)
        self.assertIn("--log_file="+str(Path("run/game.log")), argv)

    def test_duplicate_or_malformed_explicit_log_levels_are_rejected(self):
        for extra in (["--log_level=info", "--log_level=debug"],
                      ["--log_level=debug", "--log_level=debug"], ["--log_level"],
                      ["--log_level="], ["--log_level=debug=info"], ["--log_level=debug info"]):
            with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, "at most one"):
                self.arguments(extra)

    def test_protected_capture_paths_remain_unconditionally_rejected(self):
        for option in ("--cache_root", "--user_data_root", "--launcher_profile", "--log_file"):
            with self.subTest(option=option), self.assertRaisesRegex(ValueError, "isolated"):
                self.arguments(["--log_level=debug", option+"=other"])


if __name__ == "__main__":
    unittest.main()
