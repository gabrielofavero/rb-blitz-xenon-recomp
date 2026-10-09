# milotex

A small CLI over [MiloLib](https://github.com/ihatecompvir/MiloEditor) for pulling a
texture out of a Harmonix `.milo_xbox` scene and putting one back. MiloLib does the asset
parsing; this adds the two verbs its GUI offers but a script cannot drive.

```
milotex list   <scene.milo_xbox>
milotex export <scene.milo_xbox> <texture> <out.dds>
milotex import <scene.milo_xbox> <texture> <in.dds> <out.milo_xbox>
```

## Building it

MiloEditor is **not** vendored here, and neither is the build output: `bin/` and `obj/` are
gitignored, so a fresh checkout has no `milotex.exe` and has to build one. There are two
pieces to that.

1. **The .NET 8 SDK** (`dotnet --version` should answer `8.x`). Nothing else in this
   repository needs it; it is this tool's alone.

2. **A MiloEditor clone.** `MiloTex.csproj` references `MiloLib.csproj` from a sibling
   checkout, and defaults to `out/_tools/MiloEditor/MiloLib/MiloLib.csproj` — a path under
   the repository's gitignored `out/`, so cloning there needs no extra arguments:

   ```powershell
   git clone https://github.com/ihatecompvir/MiloEditor out\_tools\MiloEditor
   dotnet build tools\milotex\MiloTex.csproj -c Release
   ```

   Keeping MiloEditor somewhere else is fine; point the project at it either way:

   ```powershell
   dotnet build tools\milotex\MiloTex.csproj -c Release `
       -p:MiloLibProject=C:\path\to\MiloEditor\MiloLib\MiloLib.csproj
   ```

The build writes `tools\milotex\bin\Release\net8.0\milotex.exe`. `-c Debug` puts it
under `Debug\` instead; the `.gitignore` covers both. Nothing in `scripts/` calls it — it
is driven by hand for the texture work described in [docs/assets.md](../../docs/assets.md).

## Why it is not committed prebuilt

The exe and its `obj/` intermediate files record absolute paths — MSBuild writes the
project directory and every source path it touched into `obj/`, and the compiled `.pdb`
carries them too. A committed build output is therefore one developer's disk layout in
everyone else's checkout, which is exactly what the repository's `.gitignore` exists to
prevent.
