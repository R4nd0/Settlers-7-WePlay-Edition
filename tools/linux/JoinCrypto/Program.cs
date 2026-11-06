using System.Text.Json;

try
{
    if (args.Length != 2 || args[0] != "directory-crypto" || args[1] is not ("keygen" or "seal" or "open"))
        return 2;
    Console.WriteLine(JsonSerializer.Serialize(DirectoryCrypto.Run(args[1])));
    return 0;
}
catch
{
    Console.Error.WriteLine("directory_join_failed");
    return 2;
}
