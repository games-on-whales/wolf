namespace WolfApiServer.Models;

public record ContainerImage
{
    public required string Id { get; set; }

    public required string Name { get; set; }

    public required string Art { get; set; }

    public ImageState State { get; set; }
}

public enum ImageState
{
    UpToDate,

    OutOfDate,
}
