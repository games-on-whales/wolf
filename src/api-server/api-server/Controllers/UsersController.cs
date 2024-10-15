using Microsoft.AspNetCore.Mvc;
using WolfApiServer.Models;

namespace WolfApiServer.Controllers;

[Route("api/[controller]")]
[ApiController]
public class UsersController : ControllerBase
{
    [HttpGet]
    public IEnumerable<User> Get()
    {
        return [
            new("Fernie", new("https://avatars.akamai.steamstatic.com/939ad068bd7ef37cb1d94a81775e75923a3747f8_full.jpg")),
            new("LOCKONJARJEE", new("https://avatars.akamai.steamstatic.com/151c59e72afabc496adbd99251a2b97ef3b82abb_full.jpg")),
        ];
    }
}
