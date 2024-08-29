using WolfApiServer.Models;

namespace WolfApiServer.Data.Interfaces;

public interface ICommonData
{
    public Dictionary<int, Session> Sessions { get; }
}