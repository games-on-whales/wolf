using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using WolfApiServer.Data.Interfaces;
using WolfApiServer.Models;

namespace WolfApiServer.Controllers
{
    [Route("api/[controller]")]
    [ApiController]
    public class SessionsController(ICommonData commonData) : ControllerBase
    {
        // GET: api/<SessionsController>
        [HttpGet]
        public IEnumerable<Session> Get()
        {
            return commonData.Sessions.Values.ToList();
        }

        // GET api/<SessionsController>/5
        [HttpGet("{id:int}")]
        public ActionResult<Session> Get(int id)
        {
            if (!commonData.Sessions.ContainsKey(id))
                return NotFound();

            return commonData.Sessions[id];
        }

        // POST api/<SessionsController>
        [HttpPost]
        public ActionResult<Session> Post([FromBody] Session value)
        {
            if (!commonData.Sessions.TryAdd(value.Id, value))
                return Conflict();
            
            return CreatedAtAction(nameof(Get), new { id = value.Id }, value);
        }

        // PUT api/<SessionsController>/5
        [HttpPut("{id:int}")]
        public IActionResult Put(int id, [FromBody] Session value)
        {
            if (id != value.Id)
                return BadRequest();
            if(!commonData.Sessions.TryGetValue(id, out Session? sessionFound))
                return NotFound();
            
            commonData.Sessions[id] = value;

            return NoContent();
        }

        // DELETE api/<SessionsController>/5
        [HttpDelete("{id:int}")]
        public IActionResult Delete(int id)
        {
            if (!commonData.Sessions.Remove(id))
                return NotFound();

            return NoContent();
        }
    }
}
