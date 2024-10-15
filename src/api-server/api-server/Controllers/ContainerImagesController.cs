using Microsoft.AspNetCore.Mvc;
using WolfApiServer.Models;

namespace WolfApiServer.Controllers;

[Route("api/[controller]")]
[ApiController]
public class ContainerImagesController : ControllerBase
{
    [HttpGet]
    public IEnumerable<ContainerImage> Get()
    {
        return
        [
            new()
            {
                Id = "ghcr.io/games-on-whales/steam:edge",
                Name = "Steam",
                Art = "https://cdn2.steamgriddb.com/grid/39c2966989c4f0091a99eef7f1d09c09.png",
                State = ImageState.OutOfDate,
            },
            new()
            {
                Id = "ghcr.io/games-on-whales/firefox:edge",
                Name = "Firefox",
                Art = "https://cdn2.steamgriddb.com/grid/4529f985441a035ae4a107b8862ba4dd.png",
                State = ImageState.UpToDate,
            },
        ];
    }
}
