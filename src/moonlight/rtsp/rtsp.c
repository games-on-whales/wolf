#include "rtsp.h"

// Check if String s begins with the given prefix
static bool startsWith(const char* s, const char* prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Gets the length of the message
static int getMessageLength(PRTSP_MESSAGE msg) {
  POPTION_ITEM current;

  // Initialize to 1 for null terminator
  size_t count = 1;

  // Add the length of the protocol
  count += strlen(msg->protocol);

  // Add length of request-specific strings
  if (msg->type == TYPE_REQUEST) {
    count += strlen(msg->message.request.command);
    count += strlen(msg->message.request.target);

    // two spaces and \r\n
    count += MESSAGE_END_LENGTH;
  }
  // Add length of response-specific strings
  else {
    char statusCodeStr[16];
    sprintf(statusCodeStr, "%d", msg->message.response.statusCode);
    count += strlen(statusCodeStr);
    count += strlen(msg->message.response.statusString);
    // two spaces and \r\n
    count += MESSAGE_END_LENGTH;
  }
  // Count the size of the options
  current = msg->options;

  while (current != NULL) {
    count += strlen(current->option);
    count += strlen(current->content);
    // :[space] and \r\n
    count += MESSAGE_END_LENGTH;
    current = current->next;
  }
  // /r/n ending
  count += CRLF_LENGTH;

  count += msg->payloadLength;

  return (int)count;
}

// Given an RTSP message string rtspMessage, parse it into an RTSP_MESSAGE struct msg
int parseRtspMessage(PRTSP_MESSAGE msg, char* rtspMessage, int length) {
  char* token;
  char* protocol;
  char* endCheck;
  char* target;
  char* statusStr;
  char* command;
  char* sequence;
  char flag;
  bool messageEnded = false;

  char* payload = NULL;
  char* opt = NULL;
  int statusCode = 0;
  int sequenceNum;
  int exitCode;
  POPTION_ITEM options = NULL;
  POPTION_ITEM newOpt;

  // Delimeter sets for strtok()
  char* delim = " \r\n";
  char* end = "\r\n";
  char* optDelim = " :\r\n";
  char typeFlag = TOKEN_OPTION;

  // Put the raw message into a string we can use
  char* messageBuffer = malloc(length + 1);
  if (messageBuffer == NULL) {
    exitCode = RTSP_ERROR_NO_MEMORY;
    goto ExitFailure;
  }
  memcpy(messageBuffer, rtspMessage, length);

  // The payload logic depends on a null-terminator at the end
  messageBuffer[length] = 0;

  // Get the first token of the message
  token = strtok(messageBuffer, delim);
  if (token == NULL) {
    exitCode = RTSP_ERROR_MALFORMED;
    goto ExitFailure;
  }

  // The message is a response
  if (startsWith(token, "RTSP")) {
    flag = TYPE_RESPONSE;
    // The current token is the protocol
    protocol = token;

    // Get the status code
    token = strtok(NULL, delim);
    statusCode = atoi(token);
    if (token == NULL) {
      exitCode = RTSP_ERROR_MALFORMED;
      goto ExitFailure;
    }

    // Get the status string
    statusStr = strtok(NULL, end);
    if (statusStr == NULL) {
      exitCode = RTSP_ERROR_MALFORMED;
      goto ExitFailure;
    }

    // Request fields - we don't care about them here
    target = NULL;
    command = NULL;
  }

  // The message is a request
  else {
    flag = TYPE_REQUEST;
    command = token;
    target = strtok(NULL, delim);
    if (target == NULL) {
      exitCode = RTSP_ERROR_MALFORMED;
      goto ExitFailure;
    }
    protocol = strtok(NULL, delim);
    if (protocol == NULL) {
      exitCode = RTSP_ERROR_MALFORMED;
      goto ExitFailure;
    }
    // Response field - we don't care about it here
    statusStr = NULL;
  }
  if (strcmp(protocol, "RTSP/1.0")) {
    exitCode = RTSP_ERROR_MALFORMED;
    goto ExitFailure;
  }
  // Parse remaining options
  while (token != NULL)
  {
    token = strtok(NULL, typeFlag == TOKEN_OPTION ? optDelim : end);
    if (token != NULL) {
      if (typeFlag == TOKEN_OPTION) {
        opt = token;
      }
      // The token is content
      else {
        // Create a new node containing the option and content
        newOpt = (POPTION_ITEM)malloc(sizeof(OPTION_ITEM));
        if (newOpt == NULL) {
          exitCode = RTSP_ERROR_NO_MEMORY;
          goto ExitFailure;
        }
        newOpt->flags = 0;
        newOpt->option = opt;
        newOpt->content = token;
        newOpt->next = NULL;
        insertOption(&options, newOpt);

        // Check if we're at the end of the message portion marked by \r\n\r\n
        // endCheck points to the remainder of messageBuffer after the token
        endCheck = &token[0] + strlen(token) + 1;

        // See if we've hit the end of the message. The first \r is missing because it's been tokenized
        if (startsWith(endCheck, "\n") && endCheck[1] == '\0') {
          // RTSP over ENet doesn't always have the second CRLF for some reason
          messageEnded = true;

          break;
        }
        else if (startsWith(endCheck, "\n\r\n")) {
          // We've encountered the end of the message - mark it thus
          messageEnded = true;

          // The payload is the remainder of messageBuffer. If none, then payload = null
          if (endCheck[3] != '\0')
            payload = &endCheck[3];

          break;
        }
      }
    }
    typeFlag ^= 1; // flip the flag
  }
  // If we never encountered the double CRLF, then the message is malformed!
  if (!messageEnded) {
    exitCode = RTSP_ERROR_MALFORMED;
    goto ExitFailure;
  }

  // Get sequence number as an integer
  sequence = getOptionContent(options, "CSeq");
  if (sequence != NULL) {
    sequenceNum = atoi(sequence);
  }
  else {
    sequenceNum = SEQ_INVALID;
  }
  // Package the new parsed message into the struct
  if (flag == TYPE_REQUEST) {
    createRtspRequest(msg, messageBuffer, FLAG_ALLOCATED_MESSAGE_BUFFER | FLAG_ALLOCATED_OPTION_ITEMS, command, target,
                      protocol, sequenceNum, options, payload, payload ? length - (int)(payload - messageBuffer) : 0);
  }
  else {
    createRtspResponse(msg, messageBuffer, FLAG_ALLOCATED_MESSAGE_BUFFER | FLAG_ALLOCATED_OPTION_ITEMS, protocol, statusCode,
                       statusStr, sequenceNum, options, payload, payload ? length - (int)(payload - messageBuffer) : 0);
  }
  return RTSP_ERROR_SUCCESS;

ExitFailure:
  if (options) {
    freeOptionList(options);
  }
  if (messageBuffer) {
    free(messageBuffer);
  }
  return exitCode;
}

// Create new RTSP message struct with response data
void createRtspResponse(PRTSP_MESSAGE msg, char* message, int flags, char* protocol,
                        int statusCode, char* statusString, int sequenceNumber, POPTION_ITEM optionsHead, char* payload, int payloadLength) {
  msg->type = TYPE_RESPONSE;
  msg->flags = flags;
  msg->messageBuffer = message;
  msg->protocol = protocol;
  msg->options = optionsHead;
  msg->payload = payload;
  msg->payloadLength = payloadLength;
  msg->sequenceNumber = sequenceNumber;
  msg->message.response.statusString = statusString;
  msg->message.response.statusCode = statusCode;
}

// Create new RTSP message struct with request data
void createRtspRequest(PRTSP_MESSAGE msg, char* message, int flags,
                       char* command, char* target, char* protocol, int sequenceNumber, POPTION_ITEM optionsHead, char* payload, int payloadLength) {
  msg->type = TYPE_REQUEST;
  msg->flags = flags;
  msg->protocol = protocol;
  msg->messageBuffer = message;
  msg->options = optionsHead;
  msg->payload = payload;
  msg->payloadLength = payloadLength;
  msg->sequenceNumber = sequenceNumber;
  msg->message.request.command = command;
  msg->message.request.target = target;
}

// Retrieves option content from the linked list given the option title
char* getOptionContent(POPTION_ITEM optionsHead, char* option) {
  POPTION_ITEM current = optionsHead;
  while (current != NULL) {
    // Check if current node is what we're looking for
    if (!strcmp(current->option, option)) {
      return current->content;
    }
    current = current->next;
  }
  // Not found
  return NULL;
}

// Adds new option opt to the struct's option list
void insertOption(POPTION_ITEM* optionsHead, POPTION_ITEM opt) {
  POPTION_ITEM current = *optionsHead;
  opt->next = NULL;

  // Empty options list
  if (*optionsHead == NULL) {
    *optionsHead = opt;
    return;
  }

  // Traverse the list and insert the new option at the end
  while (current != NULL) {
    // Check for duplicate option; if so, replace the option currently there
    if (!strcmp(current->option, opt->option)) {
      current->content = opt->content;
      return;
    }
    if (current->next == NULL) {
      current->next = opt;
      return;
    }
    current = current->next;
  }
}

// Free every node in the message's option list
void freeOptionList(POPTION_ITEM optionsHead) {
  POPTION_ITEM current = optionsHead;
  POPTION_ITEM temp;
  while (current != NULL) {
    temp = current;
    current = current->next;
    if (temp->flags & FLAG_ALLOCATED_OPTION_FIELDS) {
      free(temp->option);
      free(temp->content);
    }
    free(temp);
  }
}

// Serialize the message struct into a string containing the RTSP message
char* serializeRtspMessage(PRTSP_MESSAGE msg, int* serializedLength) {
  int size = getMessageLength(msg);
  char* serializedMessage;
  POPTION_ITEM current = msg->options;
  char statusCodeStr[16];

  serializedMessage = malloc(size);
  if (serializedMessage == NULL) {
    return NULL;
  }

  if (msg->type == TYPE_REQUEST) {
    // command [space]
    strcpy(serializedMessage, msg->message.request.command);
    strcat(serializedMessage, " ");
    // target [space]
    strcat(serializedMessage, msg->message.request.target);
    strcat(serializedMessage, " ");
    // protocol \r\n
    strcat(serializedMessage, msg->protocol);
    strcat(serializedMessage, "\r\n");
  }
  else {
    // protocol [space]
    strcpy(serializedMessage, msg->protocol);
    strcat(serializedMessage, " ");
    // status code [space]
    sprintf(statusCodeStr, "%d", msg->message.response.statusCode);
    strcat(serializedMessage, statusCodeStr);
    strcat(serializedMessage, " ");
    // status str\r\n
    strcat(serializedMessage, msg->message.response.statusString);
    strcat(serializedMessage, "\r\n");
  }
  // option content\r\n
  while (current != NULL) {
    strcat(serializedMessage, current->option);
    strcat(serializedMessage, ": ");
    strcat(serializedMessage, current->content);
    strcat(serializedMessage, "\r\n");
    current = current->next;
  }
  // Final \r\n
  strcat(serializedMessage, "\r\n");

  // payload
  if (msg->payload != NULL) {
    int offset;

    // Find end of the RTSP message header
    for (offset = 0; serializedMessage[offset] != 0; offset++);

    // Add the payload after
    memcpy(&serializedMessage[offset], msg->payload, msg->payloadLength);

    *serializedLength = offset + msg->payloadLength;
  }
  else {
    *serializedLength = (int)strlen(serializedMessage);
  }

  return serializedMessage;
}

// Free everything in a msg struct
void freeMessage(PRTSP_MESSAGE msg) {
  // If we've allocated the message buffer
  if (msg->flags & FLAG_ALLOCATED_MESSAGE_BUFFER) {
    free(msg->messageBuffer);
  }

  // If we've allocated any option items
  if (msg->flags & FLAG_ALLOCATED_OPTION_ITEMS) {
    freeOptionList(msg->options);
  }

  // If we've allocated the payload
  if (msg->flags & FLAG_ALLOCATED_PAYLOAD) {
    free(msg->payload);
  }
}
