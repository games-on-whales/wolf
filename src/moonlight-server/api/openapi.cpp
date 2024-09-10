#include <api/api.hpp>

namespace wolf::api {

struct OpenAPIComponents {
  std::map<std::string, rfl::Object<rfl::Generic>> schemas;
};

struct OpenAPISchema {
  std::string openapi = "3.1.0";
  rfl::Object<rfl::Generic> info = {};
  rfl::Object<rfl::Object<rfl::Generic>> paths = {};
  OpenAPIComponents components = {};
};

struct JSONSchema {
  std::string $schema;
  std::string $ref;
  std::map<std::string, rfl::Object<rfl::Generic>> definitions;
};

template <typename T> std::string HTTPServer<T>::openapi_schema() const {
  OpenAPISchema schema = {};

  schema.info["title"] = "Wolf API";
  schema.info["description"] = "API for the Wolf server";
  schema.info["version"] = "0.1";

  /**
   * Takes a json schema in string form and returns a valid OpenAPI json object for that schema.
   * Adds these new schemas under schema.definitions in the process
   */
  auto set_json_schema = [&schema](const std::string schema_str) {
    JSONSchema request_schema = rfl::json::read<JSONSchema>(schema_str).value();

    // add all definitions to components
    for (const auto &[name, cur_schema] : request_schema.definitions) {
      schema.components.schemas[name] = cur_schema;
    }

    auto reference = rfl::Generic::Object();
    reference["$ref"] = request_schema.$ref;

    auto request_json = rfl::Generic::Object();
    request_json["schema"] = reference;

    auto request_content = rfl::Generic::Object();
    request_content["application/json"] = request_json;

    return request_content;
  };

  /**
   * Turns a APIDescription into a valid OpenAPI json object
   */
  auto api_desc_to_json = [&schema, &set_json_schema](const APIDescription &description) {
    auto json = rfl::Generic::Object();
    if (description.json_schema.has_value()) {
      json["content"] = set_json_schema(description.json_schema.value());
    }
    json["description"] = description.description;
    return json;
  };

  // Iterate over the defined local endpoints
  for (const auto &[req, handler] : endpoints_) {
    const auto [method, path] = req;

    rfl::Object<rfl::Generic> path_obj = {};
    path_obj["summary"] = handler.summary;
    path_obj["description"] = handler.description;

    if (handler.request_description.has_value()) {
      auto request = api_desc_to_json(handler.request_description.value());
      request["required"] = true;
      path_obj["requestBody"] = request;
    }

    // Iterate over the responses
    auto response_code = rfl::Generic::Object();
    for (const auto &[status_code, response_description] : handler.response_description) {
      response_code[std::to_string(status_code)] = api_desc_to_json(response_description);
    }
    path_obj["responses"] = response_code;

    schema.paths[path][utils::to_lower(rfl::enum_to_string(method))] = path_obj;
  }

  auto final_json = rfl::json::write(schema);

  // we have to replace all #/definitions/ with #/components/schemas/
  final_json = std::regex_replace(final_json, std::regex("#/definitions/"), "#/components/schemas/");
  return final_json;
}

template class HTTPServer<std::shared_ptr<UnixSocket>>;

} // namespace wolf::api