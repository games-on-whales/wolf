using WolfApiServer.Data.Interfaces;
using WolfApiServer.Models;

namespace WolfApiServer.Data;

public class CommonDataMemory : ICommonData
{
    public Dictionary<int,Session> Sessions { get; } = new();
}