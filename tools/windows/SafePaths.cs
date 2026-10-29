using System.Security.Cryptography;

namespace WePlay.Helpers;

internal static class SafePaths
{
    internal static string Plain(string path)
    {
        if(string.IsNullOrWhiteSpace(path)||path.IndexOf('\0')>=0)throw new InvalidDataException("helper_invalid");
        string full=Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        // No devices, UNC/network shares, alternate data streams or reparse traversal.
        if(full.Length<3||full[1]!=':'||full[2]!='\\'||full.AsSpan(2).Contains(':'))throw new InvalidDataException("helper_invalid");
        for(string? cursor=full;cursor!=null;cursor=Path.GetDirectoryName(cursor))
        {
            try {if((File.GetAttributes(cursor)&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("helper_invalid");}
            catch(FileNotFoundException){}catch(DirectoryNotFoundException){}
        }
        return full;
    }
    internal static string FileAt(string root,string relative)
    {
        string path=Plain(Path.Combine(root,relative));
        if(!path.StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)||!File.Exists(path))throw new InvalidDataException("helper_invalid");
        return path;
    }
    internal static string Hash(string path)
    {using var stream=File.OpenRead(Plain(path));return Convert.ToHexStringLower(SHA256.HashData(stream));}
}
