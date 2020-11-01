#include "ArduCastControl.h"

#include "cast_channel.pb.h"
#include "authority_keys.pb.h"
#include "logging.pb.h"
 
#include "pb_common.h"
#include "pb.h"
#include "pb_encode.h"

#include "string.h"

const char CC_SOURCEID[] = "sender-0";
const char CC_MAIN_DESTIID[] = "receiver-0";
const char CC_NS_CONNECTION[] = "urn:x-cast:com.google.cast.tp.connection";
const char CC_NS_RECEIVER[] = "urn:x-cast:com.google.cast.receiver";
const char CC_NS_HEARTBEAT[] = "urn:x-cast:com.google.cast.tp.heartbeat";
const char CC_NS_MEDIA[] = "urn:x-cast:com.google.cast.media";
const char CC_MSG_CONNECT[] = "{\"type\": \"CONNECT\"}";
const char CC_MSG_PING[] = "{\"type\": \"PING\"}";
const char CC_MSG_GET_STATUS[] = "{\"type\": \"GET_STATUS\", \"requestId\": 1}"; 

//incomplet msgs as these need some args to the end
const char CC_MSG_PLAY[] = "{\"type\": \"PLAY\", \"requestId\": 2, \"mediaSessionId\": ";
const char CC_MSG_PAUSE[] = "{\"type\": \"PAUSE\", \"requestId\": 2, \"mediaSessionId\": ";
const char CC_MSG_NEXT[] = "{\"type\": \"QUEUE_NEXT\", \"requestId\": 2, \"mediaSessionId\": ";
const char CC_MSG_PREV[] = "{\"type\": \"QUEUE_PREV\", \"requestId\": 2, \"mediaSessionId\": ";
const char CC_MSG_SET_VOL[] = "{\"type\": \"SET_VOLUME\", \"requestId\": 2, \"volume\": {\"level\": ";//this need double braces!
const char CC_MSG_VOL_MUTE[] = "{\"type\": \"SET_VOLUME\", \"requestId\": 2, \"volume\": {\"muted\": ";//this need double braces!
static char cc_msg_ctrl[128];

// void ArduCastConnection::init(WiFiClientSecure& client, int keepAlive, uint8_t *writeBuffer, int writeBufferSize){
//   //this->client = client;
//   this->keepAlive = keepAlive;
//   this->writeBuffer = writeBuffer;
//   this->writeBufferSize = writeBufferSize;
//   connected = false;
// }


int ArduCastConnection::connect(const char* destinationId){
  // Serial.printf("Connect to %s\n", destinationId);
  strncpy(destId, destinationId, sizeof(destId));
  int err =  writeMsg(CC_NS_CONNECTION, CC_MSG_CONNECT);
  pinged(); //do not ping immediately - we probably want to check status anyway
  connected = true;
  return err;
}

void ArduCastConnection::pinged(){
  lastMsgAt = millis();
}

void ArduCastConnection::setDisconnect(){
  connected = false;
}

channelConnection_t ArduCastConnection::getConnectionStatus(){
  if ( !client.connected() || !connected )
    return CH_DISCONNECTED;
  if ( (unsigned long)(lastMsgAt + 3*keepAlive) < millis() ){
    connected = false;
    return CH_DISCONNECTED;
  }
  if ( (unsigned long)(lastMsgAt + keepAlive) < millis())
    return CH_NEEDS_PING;
  else
    return CH_CONNECTED;
}

const char* ArduCastConnection::getDestinationId(){
  return destId;
}

bool ArduCastConnection::encode_string(pb_ostream_t *stream, const pb_field_iter_t *field, void * const *arg)
{
  const char *str = (const char*)(*arg);

  // Serial.printf("Encode: %s (%d)\n",str, strlen(str));
  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

int ArduCastConnection::writeMsg(const char* nameSpace, const char* payload){
  if ( !client.connected() )
    return -1;
   
  extensions_api_cast_channel_CastMessage newMsg = extensions_api_cast_channel_CastMessage_init_zero;
  pb_ostream_t stream = pb_ostream_from_buffer(writeBuffer+4, writeBufferSize-4);
  newMsg.source_id.arg = (void*)CC_SOURCEID;
  newMsg.source_id.funcs.encode = encode_string;
  newMsg.destination_id.arg = (void*)destId;
  newMsg.destination_id.funcs.encode = encode_string;
  newMsg.namespace_fix.arg = (void*)nameSpace;
  newMsg.namespace_fix.funcs.encode = encode_string;
  newMsg.payload_type = extensions_api_cast_channel_CastMessage_PayloadType_STRING;
  newMsg.payload_utf8.arg = (void*)payload;
  newMsg.payload_utf8.funcs.encode = encode_string;

  bool status = pb_encode(&stream, extensions_api_cast_channel_CastMessage_fields, &newMsg);
  if (!status)
    return -2;
  
  writeBuffer[0] = (stream.bytes_written>>24) & 0xFF;
  writeBuffer[1] = (stream.bytes_written>>16) & 0xFF;
  writeBuffer[2] = (stream.bytes_written>>8) & 0xFF;
  writeBuffer[3] = (stream.bytes_written>>0) & 0xFF;

  uint32_t len = client.write(writeBuffer, stream.bytes_written+4);
  if (len < stream.bytes_written+4)
    return -3;
  
  return 0;
}


////////////////////////


uint32_t ArduCastControl::getIncomingMessageLength(WiFiClientSecure &client){
  uint8_t buffer[4];
  client.peekBytes(buffer, 4);
  uint32_t len = (buffer[0]<<24) + (buffer[1]<<16) + (buffer[2]<<8) + buffer[3];
  return len;
}


uint32_t ArduCastControl::getRawMessage(uint8_t *buffer, uint16_t bufSize, WiFiClientSecure &client, uint32_t timeout){
  unsigned long start = millis();
  // Serial.printf("Checking read %d\n", client.available());
  if (bufSize <= 4 || client.available() < 4 ){
    return 0;
  }

  uint16_t len = getIncomingMessageLength(client);
  while ( client.available() < len+4 ){
    if (millis() - start > timeout ){
      // Serial.println("timeout");
      while(client.available())
        client.read();
      return 0;
    }
  }

  uint32_t dump = 0;
  if ( bufSize < len + 4 ){
    dump = len - (bufSize-4);
    len = bufSize-4;
  }
  client.readBytes(buffer, len+4);
  while ( dump > 0){
    client.read();
    dump--;
  }
  return len+4;
}



int ArduCastControl::connect(const char* host){
  client.allowSelfSignedCerts(); //chromecast seems to use self signed cert

  int err = client.connect(host, 8009);
  if ( !err ){
    return -10;
  }
  
  connectionStatus =  TCPALIVE;
  
  // deviceConnection.init(client, PING_TIMEOUT, connBuffer, CONNBUFFER_SIZE);
  // applicationConnection.init(client, PING_TIMEOUT, connBuffer, CONNBUFFER_SIZE);
  err = deviceConnection.connect(CC_MAIN_DESTIID);
  if ( err == 0 )
    connectionStatus = CONNECTED;
  return err;
}

void ArduCastControl::printRawMsg(int64_t len, uint8_t *buffer){

  Serial.printf("Message Length: %lld\n", len);
 
  Serial.print("Message: ");

  //python style print for compare
  for(int64_t i = 0; i<len; i++){
    if ( buffer[i] < 127 && buffer[i] > 31){
      Serial.printf("%c",buffer[i]);  
    } else {
      Serial.printf("\\x%02X",buffer[i]);
    }
  }
  //hex stream print for https://protogen.marcgravell.com/decode
  // for(int64_t i = 0; i<len; i++){
  //   Serial.printf("%02X",buffer[i]);
  // }
  Serial.println();
}

uint8_t ArduCastControl::pbDecodeVarint(uint8_t *bufferStart, uint32_t *decodedInt){
  *decodedInt = 0;
  int8_t decoded = -1;
  do {
    decoded++;
    //Serial.printf("curr=0x%02x, in=0x%02x, d=%d\n", *decodedInt, bufferStart[decoded], decoded);
    *decodedInt <<= 7;
    *decodedInt |= bufferStart[decoded] & 0x7f;
  } while ( bufferStart[decoded] & 0x80 );
  return decoded+1;
}

uint8_t ArduCastControl::pbDecodeHeader(uint8_t *bufferStart, uint8_t *tag, uint8_t *wire, uint32_t *lengthOrValue){
  *wire = bufferStart[0] & 0x07;
  *tag = bufferStart[0] >> 3;
  uint8_t processedBytes = 1;
  //Serial.printf("desc=0x%02x\n", bufferStart[0]);
  processedBytes += pbDecodeVarint(bufferStart+1, lengthOrValue);
  return processedBytes;
}


connection_t ArduCastControl::loop(){
  if ( !client.connected() ){
    client.stopAll();
    connectionStatus = DISCONNECTED;
    return DISCONNECTED;
  }
  uint32_t read;
  bool rxProcessed = false;
  
  //--------------------- RX code -----------------------------
  do {
    //download the msg to connBuffer (only accept what we expect)
    read = getRawMessage(connBuffer, CONNBUFFER_SIZE, client, 100);
    if ( read > 0){
      rxProcessed = true; //this will disable tx operations in this loop
      msgSent = false; //we assume this is a response to the message we sent
      errorCount = 5; //connection is alive, reset errorCount
      uint8_t processPayload = 0; //assume no need to process it
      //printRawMsg(read-4, connBuffer+4);
      uint32_t offset = 4; //skip the length field, it's not pb
      //iterate through protobuf
      do {
        uint8_t tag, wire;
        uint32_t lengthOrValue;
        
        offset += pbDecodeHeader(connBuffer+offset, &tag, &wire, &lengthOrValue);
        //check which device responded, accept it as pong
        if ( tag == extensions_api_cast_channel_CastMessage_source_id_tag ){
          if( 0 == memcmp(connBuffer+offset, deviceConnection.getDestinationId(), lengthOrValue))
          {
            //main device, process the payload of main RECEIVER_STATUS
            // Serial.println("Pong from device");
            deviceConnection.pinged();
            processPayload = 1;
          }
          if ( applicationConnection.getConnectionStatus() != CH_DISCONNECTED &&
              0 == memcmp(connBuffer+offset, applicationConnection.getDestinationId(), lengthOrValue))
          {
            //application, process the payload as MEDIA_STATUS
            // Serial.println("Pong from app");
            processPayload = 2;
            applicationConnection.pinged();
          }
        }
        //check the namespace, we're only process receiver and media
        if ( tag == extensions_api_cast_channel_CastMessage_namespace_fix_tag ){
          if( 0 == memcmp(connBuffer+offset, CC_NS_HEARTBEAT, lengthOrValue)){ //pong message, no need to process the payload
            processPayload = 0; 
          }
          if( 0 == memcmp(connBuffer+offset, CC_NS_CONNECTION, lengthOrValue)){ //must be a close message
            if ( processPayload == 1 ){
              applicationConnection.setDisconnect();
              deviceConnection.setDisconnect();
              connectionStatus = TCPALIVE;
              processPayload = false;
            } else if ( processPayload == 2) {
              applicationConnection.setDisconnect();
              processPayload = false;
            }
          }
        }

        if ( processPayload > 0 && tag == extensions_api_cast_channel_CastMessage_payload_utf8_tag ){
          DynamicJsonDocument doc(JSONBUFFER_SIZE);
          DeserializationError error = deserializeJson(doc, connBuffer+offset, lengthOrValue);
          if ( !error && doc.containsKey("type") && doc.containsKey("status") ){ //it pretty much must contain it
            if ( processPayload == 1 && strcmp("RECEIVER_STATUS", doc["type"].as<char*>()) == 0 ) {
              //save the generic info
              if ( doc["status"].containsKey("volume") ){
                if( doc["status"]["volume"].containsKey("level")){
                  volume = doc["status"]["volume"]["level"];
                } else
                  volume = -1.0;
                
                if( doc["status"]["volume"].containsKey("muted"))
                  isMuted = doc["status"]["volume"]["muted"].as<bool>();
                else
                  isMuted = false;
              
              } else {
                volume = -1.0;
                isMuted = false;
              }
              if ( doc["status"].containsKey("applications") ){
                if ( doc["status"]["applications"][0].containsKey("sessionId") ){
                  strncpy(sessionId, doc["status"]["applications"][0]["sessionId"].as<char*>() ,sizeof(sessionId));
                  sessionId[sizeof(sessionId)-1] = '\n';
                  connectionStatus = CONNECT_TO_APPLICATION;
                } else 
                  sessionId[0] = '\n';
                if ( doc["status"]["applications"][0].containsKey("statusText") ){
                  strncpy(statusText, doc["status"]["applications"][0]["statusText"].as<char*>() ,sizeof(statusText));
                  statusText[sizeof(statusText)-1] = '\n';
                } else
                  statusText[0] = '\n';
                if ( doc["status"]["applications"][0].containsKey("displayName") ){
                  strncpy(displayName, doc["status"]["applications"][0]["displayName"].as<char*>() ,sizeof(displayName));
                  displayName[sizeof(displayName)-1] = '\n';
                } else
                  displayName[0] = '\n';
              } else {
                sessionId[0] = '\0';
                statusText[0] = '\0';
                displayName[0] = '\0';
              }
            }
            if ( processPayload == 2 && strcmp("MEDIA_STATUS", doc["type"].as<char*>()) == 0 ) {
              if ( doc["status"][0].containsKey("mediaSessionId") )
                mediaSessionId = doc["status"][0]["mediaSessionId"];
              else
                mediaSessionId = -1;
              
              if ( doc["status"][0].containsKey("currentTime") )
                currentTime = doc["status"][0]["currentTime"];
              else
                currentTime = 0.0;
            
              if ( doc["status"][0].containsKey("playerState") ){
                if ( strcmp("IDLE", doc["status"][0]["playerState"].as<char*>()) == 0 ){
                  playerState = IDLE;
                } else if ( strcmp("BUFFERING", doc["status"][0]["playerState"].as<char*>()) == 0 ){
                  playerState = BUFFERING;
                } else if ( strcmp("PLAYING", doc["status"][0]["playerState"].as<char*>()) == 0 ){
                  playerState = PLAYING;
                } else if ( strcmp("PAUSED", doc["status"][0]["playerState"].as<char*>()) == 0 ){
                  playerState = PAUSED;
                } else {
                  playerState = IDLE;
                }
              } else
                playerState = IDLE;
              
              if ( doc["status"][0].containsKey("media")){
                if ( doc["status"][0]["media"].containsKey("duration") ){
                  duration = doc["status"][0]["media"]["duration"];
                } else {
                  duration = 0.0;
                }
                if ( doc["status"][0]["media"].containsKey("metadata") ){
                  if ( doc["status"][0]["media"]["metadata"].containsKey("title") ){
                    strncpy(title, doc["status"][0]["media"]["metadata"]["title"].as<char*>() ,sizeof(title));
                    title[sizeof(title)-1] = '\n';
                  } else {
                    title[0] = '\0';
                  }
                  if ( doc["status"][0]["media"]["metadata"].containsKey("artist") ){
                    strncpy(artist, doc["status"][0]["media"]["metadata"]["artist"].as<char*>() ,sizeof(artist));
                    artist[sizeof(artist)-1] = '\n';
                  } else {
                    artist[0] = '\0';
                  }
                } else {
                  title[0] = '\n';
                  artist[0] = '\n';  
                }
              } else {
                //CC seems to skip sending this when it's busy, so we ignore the error
                // duration = 0.0;
                // title[0] = '\n';
                // artist[0] = '\n';
              }
            }
          }
          // serializeJsonPretty(doc, Serial);
          // Serial.println();
        }




        //DEBUG code -- this does not work when DynamicJson is used, probably not enough heap
        // Serial.printf("T:%d, W:%d", tag, wire);
        // if ( wire == 2 ){
        //   Serial.printf(" (%d): ", lengthOrValue);
        //   Serial.printf("%.*s\n", lengthOrValue, connBuffer+offset);
        // } else {
        //   Serial.printf(": %d\n", lengthOrValue);
        // }
        //end of debug code


        //for length delimited stuff, add the decoded length to the offset
        if ( wire == 2)
          offset+=lengthOrValue;
      } while(offset<read);
    }
  } while ( read > 0);
  
  // ---------------- TX code ------------------------
  //don't send msg if we just received one; wait 500ms for an answer
  if ( !rxProcessed && (!msgSent || ((millis() - msgSentAt) > 500 ))){ 
    //handle broken links
    if ( msgSent ){
      Serial.printf("EC:%d\n", errorCount);
      if (--errorCount == 0 ){
        client.stopAll();
        connectionStatus = DISCONNECTED;
        msgSent = false;
        return DISCONNECTED;
      }
    }

    // Serial.println("Preparing for msg");
    msgSent = false;
    int err = 0;
    if ( connectionStatus == CONNECT_TO_APPLICATION ){
      // Serial.print("CA");
      err = applicationConnection.connect(sessionId);
      if ( err == 0 )
        connectionStatus = CONNECTED;
    } else if ( applicationConnection.getConnectionStatus() == CH_DISCONNECTED ){
      // Serial.print("GS");
      err = deviceConnection.writeMsg(CC_NS_RECEIVER, CC_MSG_GET_STATUS);
      if ( err == 0 ) {
        msgSentAt = millis();
        msgSent = true;
      }
    } else if ( deviceConnection.getConnectionStatus() == CH_NEEDS_PING ){
      // Serial.print("ping main");
      err = deviceConnection.writeMsg(CC_NS_HEARTBEAT, CC_MSG_PING);
      if ( err == 0 ) {
        msgSentAt = millis();
        msgSent = true;
      }
    } else if ( applicationConnection.getConnectionStatus() == CH_CONNECTED ){
      // Serial.print("GSA");
      err = applicationConnection.writeMsg(CC_NS_MEDIA, CC_MSG_GET_STATUS);
      if ( err == 0 ) {
        msgSentAt = millis();
        msgSent = true;
      }
    } else if ( applicationConnection.getConnectionStatus() == CH_NEEDS_PING ){ //this will never happen, unless loop is called rarely
      // Serial.print("ping app");
      err = applicationConnection.writeMsg(CC_NS_HEARTBEAT, CC_MSG_PING);
      if ( err == 0 ) {
        msgSentAt = millis();
        msgSent = true;
      }
    }
    // if (msgSent ){
    //    Serial.printf("Res: %d\n", err);
    // }
  }

  return getConnection();
}

connection_t ArduCastControl::getConnection(){
  if ( msgSent )
    return WAIT_FOR_RESPONSE;
  if ( applicationConnection.getConnectionStatus() != CH_DISCONNECTED )
    return APPLICATION_RUNNING;
  
  return connectionStatus;
}

void ArduCastControl::dumpStatus(){
  if ( getConnection() != DISCONNECTED && getConnection() != TCPALIVE ){
    Serial.printf("V:%f%c\n", volume, isMuted?'M':' ');
    if ( applicationConnection.getConnectionStatus() != CH_DISCONNECTED ){
      Serial.printf("D:%s\n", displayName);
      Serial.printf("S:%s\n", statusText);
      Serial.printf("A/T:%s/%s\n", artist,  title);
      Serial.printf("S:%d %f/%f\n", playerState, duration, currentTime);
    }
  }
}



int ArduCastControl::play(){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;
  
  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%d}", CC_MSG_PLAY, mediaSessionId );
  return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
}

int ArduCastControl::pause(bool toggle){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;
  
  if ( toggle && playerState == PAUSED )
    return play();
  else {
    snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%d}", CC_MSG_PAUSE, mediaSessionId );
    return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
  }
}

int ArduCastControl::prev(){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;

  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%d}", CC_MSG_PREV, mediaSessionId );
  return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
}

int ArduCastControl::next(){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;
  
  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%d}", CC_MSG_NEXT, mediaSessionId );
  return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
}

int ArduCastControl::seek(bool relative, float seekTo){
  if ( msgSent )
    return -10;
  if ( mediaSessionId < 0 )
    return -9;
  
  if ( relative )
    seekTo += currentTime;
  
  if ( seekTo < 0 )
    seekTo = 0;
  if ( seekTo > duration )
    seekTo = duration;

  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "{\"type\": \"SEEK\", \"requestId\": 2, \"mediaSessionId\": %d, \"currentTime\": %f}", mediaSessionId, seekTo);
  return applicationConnection.writeMsg(CC_NS_MEDIA, cc_msg_ctrl);
}

int ArduCastControl::setVolume(bool relative, float volumeTo){
  if ( msgSent )
    return -10;

  if ( relative )
    volumeTo += volume;
  
  if ( volumeTo < 0 )
    volumeTo = 0;
  if ( volumeTo > 1 )
    volumeTo = 1;
  
  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%f}}", CC_MSG_SET_VOL, volumeTo );
  return deviceConnection.writeMsg(CC_NS_RECEIVER, cc_msg_ctrl);
}

int ArduCastControl::setMute(bool newMute, bool toggle){
  if ( msgSent )
    return -10;

  if ( toggle )
    newMute = !isMuted;
  
  snprintf(cc_msg_ctrl, sizeof(cc_msg_ctrl), "%s%s}}", CC_MSG_VOL_MUTE, newMute?"true":"false" );
  return deviceConnection.writeMsg(CC_NS_RECEIVER, cc_msg_ctrl);

}