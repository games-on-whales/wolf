namespace WolfApiServer.Models
{
    public record User(string Name, Uri? ProfileImage = null);
}