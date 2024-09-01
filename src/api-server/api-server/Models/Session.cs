namespace WolfApiServer.Models;

public class Session
{
    public int Id { get; set; }

    public DateTime CreatedAt { get; set; }

    public required string ImageId { get; set; }

    public required string ClientName { get; set; }
}