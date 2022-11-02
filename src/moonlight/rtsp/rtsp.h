#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define TYPE_REQUEST 0
#define TYPE_RESPONSE 1

#define TOKEN_OPTION 0

#define RTSP_ERROR_SUCCESS 0
#define RTSP_ERROR_NO_MEMORY -1
#define RTSP_ERROR_MALFORMED -2

#define SEQ_INVALID -1

#define FLAG_ALLOCATED_OPTION_FIELDS 0x1
#define FLAG_ALLOCATED_MESSAGE_BUFFER 0x2
#define FLAG_ALLOCATED_OPTION_ITEMS 0x4
#define FLAG_ALLOCATED_PAYLOAD 0x8

#define CRLF_LENGTH 2
#define MESSAGE_END_LENGTH (2 + CRLF_LENGTH)

typedef struct _OPTION_ITEM {
  char flags;
  char* option;
  char* content;
  struct _OPTION_ITEM* next;
} OPTION_ITEM, *POPTION_ITEM;

// In this implementation, a flag indicates the message type:
// TYPE_REQUEST = 0
// TYPE_RESPONSE = 1
typedef struct _RTSP_MESSAGE {
  char type;
  char flags;
  int sequenceNumber;
  char* protocol;
  POPTION_ITEM options;
  char* payload;
  int payloadLength;

  char* messageBuffer;

  union {
    struct {
      // Request fields
      char* command;
      char* target;
    } request;
    struct {
      // Response fields
      char* statusString;
      int statusCode;
    } response;
  } message;
} RTSP_MESSAGE, *PRTSP_MESSAGE;

int parseRtspMessage(PRTSP_MESSAGE msg, char* rtspMessage, int length);
void freeMessage(PRTSP_MESSAGE msg);
void createRtspResponse(PRTSP_MESSAGE msg, char* messageBuffer, int flags, char* protocol, int statusCode, char* statusString, int sequenceNumber, POPTION_ITEM optionsHead, char* payload, int payloadLength);
void createRtspRequest(PRTSP_MESSAGE msg, char* messageBuffer, int flags, char* command, char* target, char* protocol, int sequenceNumber, POPTION_ITEM optionsHead, char* payload, int payloadLength);
char* getOptionContent(POPTION_ITEM optionsHead, char* option);
void insertOption(POPTION_ITEM* optionsHead, POPTION_ITEM opt);
void freeOptionList(POPTION_ITEM optionsHead);
char* serializeRtspMessage(PRTSP_MESSAGE msg, int* serializedLength);
