// milotex - list, export and import the textures inside a Harmonix .milo_xbox scene.
//
// Why this exists: the scene format is already solved by the community, in MiloLib
// (https://github.com/ihatecompvir/MiloEditor). Its GUI can view, export and import a
// texture as DDS, but a GUI cannot be driven from a build script, and MiloUtil's
// `extract` only dumps an asset's raw bytes. This wraps the same MiloLib calls in the
// two verbs a reskin actually needs.
//
// The Xbox 360 storage quirk is MiloLib's, not ours: a 360 bitmap keeps its bytes in
// 4-byte groups as [1][0][3][2]. ConvertToImage() undoes it on export and this file
// undoes it on import, which is the inverse of what ConvertToImage does.
//
//   milotex list   <scene.milo_xbox>
//   milotex export <scene.milo_xbox> <texture> <out.dds>
//   milotex import <scene.milo_xbox> <texture> <in.dds> <out.milo_xbox>

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using MiloLib;
using MiloLib.Assets;
using MiloLib.Assets.Rnd;
using MiloLib.Classes;
using MiloLib.Utils;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Usage();
            return 2;
        }
        try
        {
            switch (args[0].ToLowerInvariant())
            {
                case "list":
                    return List(args[1]);
                case "export":
                    return Export(args[1], args[2], args.Length > 3 ? args[3] : null);
                case "import":
                    return Import(args[1], args[2], args[3], args[4]);
                default:
                    Usage();
                    return 2;
            }
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"milotex: {error.Message}");
            return 1;
        }
    }

    private static void Usage()
    {
        Console.Error.WriteLine(
            "usage: milotex list   <scene.milo_xbox>\n" +
            "       milotex export <scene.milo_xbox> <texture> <out.dds>\n" +
            "       milotex import <scene.milo_xbox> <texture> <in.dds> <out.milo_xbox>");
    }

    /// Every texture in a scene, including those inside inlined subdirectories, which is
    /// where most of a UI scene's art lives.
    private static List<(string dir, DirectoryMeta.Entry entry)> Textures(MiloFile milo)
    {
        var found = new List<(string, DirectoryMeta.Entry)>();
        Collect(milo.dirMeta, "", found);
        return found;
    }

    private static void Collect(DirectoryMeta meta, string prefix,
                                List<(string, DirectoryMeta.Entry)> found)
    {
        if (meta == null)
        {
            return;
        }
        foreach (var entry in meta.entries)
        {
            if (entry.obj is RndTex)
            {
                found.Add((prefix + meta.name, entry));
            }
        }
        if (meta.directory is ObjectDir dir)
        {
            foreach (var sub in dir.inlineSubDirs)
            {
                Collect(sub, prefix + meta.name + "/", found);
            }
        }
    }

    private static RndTex Find(MiloFile milo, string name)
    {
        var matches = Textures(milo)
            .Where(t => string.Equals(t.entry.name, name, StringComparison.OrdinalIgnoreCase))
            .ToList();
        if (matches.Count == 0)
        {
            throw new Exception($"no texture named '{name}' in this scene");
        }
        if (matches.Count > 1)
        {
            throw new Exception($"'{name}' is ambiguous ({matches.Count} matches); " +
                                "use a name unique to one subdirectory");
        }
        return (RndTex)matches[0].entry.obj;
    }

    private static int List(string scenePath)
    {
        var milo = new MiloFile(scenePath);
        var textures = Textures(milo);
        Console.WriteLine($"{scenePath}: {textures.Count} texture(s)");
        foreach (var (dir, entry) in textures)
        {
            var tex = (RndTex)entry.obj;
            var bmp = tex.bitmap;
            Console.WriteLine($"  {entry.name,-34} {bmp.width,5}x{bmp.height,-5} " +
                              $"{bmp.encoding,-10} bpp {bmp.bpp,-3} mips {bmp.mipMaps,-2} " +
                              $"platform {bmp.platform}  [{dir}]");
        }
        return 0;
    }

    private static int Export(string scenePath, string name, string outPath)
    {
        var milo = new MiloFile(scenePath);
        var tex = Find(milo, name);
        List<byte> dds = tex.bitmap.ConvertToImage();
        if (dds.Count == 0)
        {
            throw new Exception($"'{name}' is {tex.bitmap.encoding}, which has no DDS export");
        }
        outPath ??= name + ".dds";
        File.WriteAllBytes(outPath, dds.ToArray());
        Console.WriteLine($"exported '{name}' ({tex.bitmap.width}x{tex.bitmap.height} " +
                          $"{tex.bitmap.encoding}, {tex.bitmap.mipMaps} mip level(s)) to {outPath}");
        return 0;
    }

    private static int Import(string scenePath, string name, string inPath, string outPath)
    {
        var milo = new MiloFile(scenePath);
        var tex = Find(milo, name);

        DDS dds;
        using (var stream = File.OpenRead(inPath))
        using (var reader = new EndianReader(stream, Endian.LittleEndian))
        {
            dds = new DDS().Read(reader);
        }

        var bmp = tex.bitmap;
        if (dds.dwWidth != bmp.width || dds.dwHeight != bmp.height)
        {
            throw new Exception($"'{name}' is {bmp.width}x{bmp.height}; the replacement is " +
                                $"{dds.dwWidth}x{dds.dwHeight}. Dimensions must match - the " +
                                "archive index records each entry's size.");
        }

        bmp.width = (ushort)dds.dwWidth;
        bmp.height = (ushort)dds.dwHeight;
        bmp.bpp = (byte)dds.pf.dwRGBBitCount;
        bmp.mipMaps = (byte)(dds.dwMipMapCount - 1);

        switch (dds.pf.dwFourCC)
        {
            case 0x31545844: // DXT1
                bmp.encoding = RndBitmap.TextureEncoding.DXT1_BC1;
                bmp.bpp = 4;
                break;
            case 0x35545844: // DXT5
                bmp.encoding = RndBitmap.TextureEncoding.DXT5_BC3;
                bmp.bpp = 8;
                break;
            case 0x32495441: // ATI2
                bmp.encoding = RndBitmap.TextureEncoding.ATI2_BC5;
                bmp.bpp = 8;
                break;
            default:
                throw new Exception($"the replacement is not DXT1/DXT5/ATI2 " +
                                    $"(fourCC 0x{dds.pf.dwFourCC:X8})");
        }
        bmp.bpl = (ushort)((bmp.bpp * bmp.width) / 8);
        tex.width = dds.dwWidth;
        tex.height = dds.dwHeight;
        tex.bpp = bmp.bpp;

        // ConvertToImage splits the DDS into mip levels and swaps each for 360; both have
        // to be reversed here, or the engine reads the texture as noise.
        int levelSize = (int)(dds.dwWidth * dds.dwHeight * bmp.bpp / 8);
        var levels = new List<List<byte>>();
        for (int at = 0; at < dds.pixels.Count; at += Math.Max(1, levelSize))
        {
            int take = Math.Min(levelSize, dds.pixels.Count - at);
            levels.Add(dds.pixels.GetRange(at, take));
        }
        if (bmp.platform == DirectoryMeta.Platform.Xbox)
        {
            foreach (var level in levels)
            {
                for (int i = 0; i + 3 < level.Count; i += 4)
                {
                    (level[i], level[i + 1]) = (level[i + 1], level[i]);
                    (level[i + 2], level[i + 3]) = (level[i + 3], level[i + 2]);
                }
            }
        }
        bmp.textures = levels;

        milo.Save(outPath, milo.compressionType);
        Console.WriteLine($"imported {inPath} into '{name}' and wrote {outPath} " +
                          $"({new FileInfo(outPath).Length} bytes)");
        return 0;
    }
}
