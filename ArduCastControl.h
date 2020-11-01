/**
 * ArduCastControl.h - Arduino library to control Chromecast
 * Created by Andras Biro, November 1, 2020
 * https://github.com/andrasbiro/chromecastcontrol
 */

#include <stdint.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "pb.h"


/**
 * Buffer size for JSON deconding used with ArduinoJson's dynamic allocation.
 * Allocated from heap.
 */
#ifndef JSONBUFFER_SIZE
#define JSONBUFFER_SIZE 4096
#endif

/**
 * Common buffer used for both write and read a single protocolbuffer message.
 * Allocated with the class.
 * Biggest write is about 300B (seek), read can be much bigger. Maximum seems
 * to be about 2k, this is set to 4k for future proofing.
 */
#ifndef CONNBUFFER_SIZE
#define CONNBUFFER_SIZE 4096
#endif 

/**
 * Timeout for ping. If there was no received message for this amount of time
 * on a given channel, a PING message will be sent.
 */
#ifndef PING_TIMEOUT
#define PING_TIMEOUT 5000
#endif



/**
 * Possible connection status for \ref ArduCastConnection
 */
typedef enum channelConnection_t{
  CH_DISCONNECTED,        ///< Disconnected. Either the TCP channel or the application
  CH_NEEDS_PING,          ///< Timeout reached and a PING message should be sent. After successful PONG, \ref pinged() should be called to reset this state
  CH_CONNECTED,           ///< Connected. Both TCP and application layer.
}channelConnection_t;

/**
 * Class to maintain a chromecast connection channel. A typicial application
 * needs two:
 * One for the device and one for the application (casting) running on the
 * device.
 * 
 * Maintains a bare minimum for the connection. Doesn't write or read the
 * channel, but maintains timer for ping, holds a destination ID,
 * and provides a simpler function to write a protocol buffer message.
 * 
 * Typcially this is not needed from the application, only from
 * \ref ArduCastControl.
 */
class ArduCastConnection {
  private:
    WiFiClientSecure& client;
    const int keepAlive;
    uint8_t *const writeBuffer;
    const int writeBufferSize;
    
    channelConnection_t connectionStatus = CH_DISCONNECTED;
    char destId[50];
    unsigned long lastMsgAt = 0;
    bool connected = false;

    /**
     * Encoder function required for protocol buffer encoding
     */
    static bool encode_string(pb_ostream_t *stream, const pb_field_iter_t *field, void * const *arg);
  public:
    /**
     * Constructor
     * \param[in] _client
     *    Reference of already connected secure TCP client. Shared between
     *    multiple classes
     * \param[in] _keepAlive
     *    Timeout ater CH_NEEDS_PING is set
     * \param[in] _writeBuffer
     *    Buffer to use by \ref writeMsg(). Shared between multiple classes
     * \param[in] _writeBufferSize
     *    Size of \ref _writeBuffer
     */
    ArduCastConnection(WiFiClientSecure &_client, int _keepAlive, uint8_t *_writeBuffer, int _writeBufferSize)
      : client(_client), keepAlive(_keepAlive), writeBuffer(_writeBuffer), writeBufferSize(_writeBufferSize)
      {};
    
    /**
     * Connect to an application level channel. This will write a CONNECT
     * message to the TCP channel.
     * \param[in] destinationId
     *    Destination to connect. This will be stored and used for every
     *    subsecvent \ref writeMsg() as destination.
     * \return 
     *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
     *    failed, -3 if TCP channel didn't accept the whole message
     */
    int connect(const char* destinationId);

    /**
     * Resets \ref CH_NEEDS_PING status. Should be called if a message is
     * received on this channel.
     */
    void pinged();

    /**
     * Sets the status of the channel to \ref CH_DISCONNECT
     * Should be called e.g. if DISCONNECT message was received.
     */
    void setDisconnect();

    /**
     * Returns the current connection status of this channel
     * \return
     *    The current connection status.
     */
    channelConnection_t getConnectionStatus();

    /**
     * Returns the destination ID of this channel
     * \return
     *    Pointer to the destination ID string.
     */
    const char* getDestinationId();

    /**
     * Writes a message to this channel, to the stored destination ID
     * \param[in] nameSpace
     *    The namespace to write, e.g. urn:x-cast:com.google.cast.receiver
     * \param[in] payload
     *    The payload to write
     * \return 
     *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
     *    failed, -3 if TCP channel didn't accept the whole message
     */
    int writeMsg(const char* nameSpace, const char* payload);
};


/**
 * Possible connection status for \ref ArduCastControl
 */
typedef enum connection_t{
  DISCONNECTED,             ///< Disconnected. TCP channel is not open
  TCPALIVE,                 ///< TCP is connected, but the application layer connection is not alive
  CONNECTED,                ///< Both TCP and application layer is connected. No application is running on Chromecast
  APPLICATION_RUNNING,      ///< Application is running on the chromecase (i.e. something is casting)
  WAIT_FOR_RESPONSE,        ///< A message was sent and the response should be polled soon with a call to \ref loop()
  CONNECT_TO_APPLICATION,   ///< Application is running, but connection is not yet established to it. \ref loop() should be called to connect
} connection_t;

/**
 * Possible values for \ref playerState
 * See https://developers.google.com/cast/docs/reference/chrome/chrome.cast.media#.PlayerState
 */
typedef enum playerState_t{
  IDLE,                     ///< No media is loaded into the player.
  PLAYING,                  ///< The media is playing.
  PAUSED,                   ///< The media is not playing.
  BUFFERING,                ///< Player is in PLAY mode but not actively playing content. currentTime will not change.
} playerState_t;

/**
 * Main class. This class can be used to connect to a chromecast device,
 * poll information from it, like what is currently cast to it and control
 * the playback/volume on it.
 */
class ArduCastControl {
private:
  uint8_t connBuffer[CONNBUFFER_SIZE];

  connection_t connectionStatus = DISCONNECTED;
  char sessionId[50];
  int32_t mediaSessionId;
  WiFiClientSecure client;
  uint8_t errorCount = 5;

  //IPAddress ccAddress = IPAddress(192, 168, 1, 12);//FIXME 

  /**
   * Channel connection to the chromecast device itself (receiver-0)
   */
  ArduCastConnection deviceConnection = ArduCastConnection(client, PING_TIMEOUT, connBuffer, CONNBUFFER_SIZE);

  /**
   * Channel connection to the application running on chromecast, if any.
   */
  ArduCastConnection applicationConnection = ArduCastConnection(client, PING_TIMEOUT, connBuffer, CONNBUFFER_SIZE);

  /**
   * Downloads a message from the TCP channel. Chromecast messages start with
   * the length coded in 4 bytes, this function will download based on that.
   * The length field will be included in the downloaded message.
   * 
   * \param[out] buffer
   *    The buffer where the message will be written
   * \param[in] bufSize
   *    Size of the buffer
   * \param[in] client
   *    Reference to the client which should be a connected secure TCP client
   * \param[in] timeout
   *    Timeout in ms. If a message can't be downloaded in this time, the
   *    client will be purged for remaining data and the function returns.
   * \return 
   *    The amount of data read in bytes. 0 on timeout or if there's no data
   *    to read.
   */
  uint32_t getRawMessage(uint8_t *buffer, uint16_t bufSize, WiFiClientSecure &client, uint32_t timeout);

  /**
   * Helper function for \ref getRawMessage() to decode the length field of the
   * message. Does not read from the channel, it uses peek() functions.
   * 
   * \param[in] client
   *    Reference to the client which should be a connected secure TCP client,
   *    with at least 4 bytes available to read.
   * \return
   *    The length of the message on \ref client.
   */
  uint32_t getIncomingMessageLength(WiFiClientSecure &client);

  /**
   * Debug function. Prints a protocol buffer message similarly how python
   * prints bytelists.
   * \param[in] len
   *    The length of the message in bytes.
   * \param[in] buffer
   *    The buffer which holds the message. Note that the length field
   *    (first 4 bytes) in chromecast messages are not part of the protocol
   *    buffer message, and it shouldn't be passed here.
   */
  void printRawMsg(int64_t len, uint8_t *buffer);

  uint8_t pbDecodeVarint(uint8_t *bufferStart, uint32_t *decodedInt);

  /**
   * This is a very limited protobuf decoder, especially designed for
   * chromecast's cast_channel messages. It supports unsigned varints up to
   * 32 bits, in which case the value is returned in \ref lengthOrValue.
   * It also supports length-delimited headers (e.g. strings), in which case
   * \ref lengthOrValue is the length. In this case, the sting/bytestream
   * is not processed, but it can be easily accessed by 
   * \ref bufferStart + ret.
   * 
   * \param[in] bufferStart
   *    The buffer where processing should start. This should point to a
   *    protocol buffer header.
   * \param[out] tag
   *    The tag decoded from the protocol buffer header (i.e. the argument's
   *    number in the ordered list)
   * \param[out] wire
   *    The wire decoded from the protocol buffer header. Should be either
   *    0 (varint) or 2 (length-delimited). Otherwise the processing probably
   *    failed
   * \param[out] lengthOrValue
   *    The decoded value for varint (\ref wire is 0) or the length of the
   *    length-delimited type's length (\ref wire is 2)
   * \return
   *    The number of bytes processed
   */
  uint8_t pbDecodeHeader(uint8_t *bufferStart, uint8_t *tag, uint8_t *wire, uint32_t *lengthOrValue);

  unsigned long msgSentAt;
  bool msgSent;

public:
  //stuff reported by chromecast's main channel

  /**
   * displayName reported by chromecast or "" if nothing is reported.
   * Note that this is an UTF8 string
   * E.g. "Spotify"
   */
  char displayName[50];

  /**
   * statusText reported by chromecast or "" if nothing is reported.
   * Note that this is an UTF8 string
   * E.g. "Casting: <Title of the song>"
   */
  char statusText[50];

  /**
   * Volume reported by chromecast or -1 if nothing is reported
   * Should be between 0 and 1.
   */
  float volume; //0-1, -1 if nothing is reported

  /**
   * True if chromecast reported muted status, false otherwise
   */
  bool isMuted;

  //only valid when application is running, otherwise not even cleared

  /**
   * playerState reported by the application or IDLE when nothing is reported
   * E.g. PLAYING
   */
  playerState_t playerState;

  /**
   * Duration of the song currently playing (if any) in seconds or 0
   * if nothing is reported
   */
  float duration;

  /**
   * Current time in the song currently playing (if any) in seconds or 0
   * if nothing is reported
   */
  float currentTime;

  /**
   * Title of song currently playing or "" if nothing is reported.
   * Note that this is an UTF8 string
   */
  char title[50];
  
  /**
   * Artist of song currently playing or "" if nothing is reported.
   * Note that this is an UTF8 string
   */
  char artist[50];

  /**
   * Constructor
   */
  ArduCastControl(){}

  /**
   * Connect to chromecast. First connects to the TCP/TLS port with
   * self-signed certificates allowed, then connects to the main channel
   * of the chromecast application layer.
   * 
   * \param[in] host
   *    Host of the device to connect.
   * 
   * \return 
   *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
   *    failed, -3 if TCP channel didn't accept the whole message, -10 if
   *    the TCP/TLS channel can't be opened.
   */
  int connect(const char* host);

  /**
   * Returns the current connection status
   * 
   * \return
   *    The current connection status
   */
  connection_t getConnection();

  /**
   * Loop function, intended to be called periodically.
   * \li First checks if the connection is alive and returns \ref DISCONNECTED
   *    if not.
   * \li Then checks and downlads all messages available on the TCP/TLS channel,
   *    set ping status of application channels, handles disconnect requests
   *    and updates status variables (e.g. \ref volume or \ref title).
   *    If there was anything read, the function returns.
   * \li If notheing was read the function continous with writing a message.
   *    It only writes, if nothing was written in the last 500ms where an
   *    answer is expected. It writes a single message in the following order
   *    of priority:
   *    1: Connect to application if status is \ref CONNECT_TO_APPLICATION
   *    2: Get status from main channel if no application is running
   *    3: Ping on the main channel if needed
   *    4: Get status from the application if it's running
   *    5: Ping the application channel if needed (which shouldn't happen due to 4)
   * \return
   *    The current connection status, at the end of the loop function.
   */
  connection_t loop();

  /**
   * Dumps the recorded status values to Serial in the following format:
   * "V:<volume><muted>"
   * "D:<displayName>"
   * "S:<statusText>"
   * "A/T:<artist>/<title>"
   * "S:<playerState> <duration>:<currentSeek>" 
   * 
   * When no application is running, only the volume line is printed.
   * <volume> is float, <muted> is M when muted, nothing otherwise.
   * Strings are printed as is, UTF8 special characters included
   * <playerState> is printed as int, e.g. 2 is PAUSED
   * <duration> and <currentSeek> are both float in seconds.
   */
  void dumpStatus();

  /**
   * Play command (e.g. to resume paused playback)
   * 
   * \return 
   *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
   *    failed, -3 if TCP channel didn't accept the whole message, -10 if
   *    system is waiting for a response and -9 if the current media
   *    can't be identified (e.g. media was changed)
   */
  int play();

  /**
   * Pause or resume playback.
   * \param[in] toggle
   *    If false, the function will send a PAUSE command
   *    If true, the function checks the current \ref playerState and send
   *    PAUSE if playing or PLAY if paused.
   * \return 
   *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
   *    failed, -3 if TCP channel didn't accept the whole message, -10 if
   *    system is waiting for a response and -9 if the current media
   *    can't be identified (e.g. media was changed)
   */
  int pause(bool toggle);

  /**
   * Previous command. Jumps to the beginning of track or previous track.
   * 
   * \return 
   *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
   *    failed, -3 if TCP channel didn't accept the whole message, -10 if
   *    system is waiting for a response and -9 if the current media
   *    can't be identified (e.g. media was changed)
   */
  int prev();

  /**
   * Next command. Jumps to the next track,
   *
   * \return 
   *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
   *    failed, -3 if TCP channel didn't accept the whole message, -10 if
   *    system is waiting for a response and -9 if the current media
   *    can't be identified (e.g. media was changed)
   */
  int next();

  /**
   * Seek to the requested position in media
   * \param[in] relative
   *    If false, seeks to \ref seekTo, if true, seeks to
   *    \ref seekTo + \ref currentTime
   * \param[in] seekTo
   *    Position to seek to, either in relative or absolute
   * 
   * \return 
   *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
   *    failed, -3 if TCP channel didn't accept the whole message, -10 if
   *    system is waiting for a response and -9 if the current media
   *    can't be identified (e.g. media was changed)
   */
  int seek(bool relative, float seekTo);

  /**
   * Sets the volume
   * \param[in] relative
   *    If false, sets to \ref volumeTo, if true, seeks to
   *    \ref volumeTo + \ref volume
   * \param[in] volumeTo
   *    Volume to set, either in relative or absolute
   * 
   * \return 
   *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
   *    failed, -3 if TCP channel didn't accept the whole message, -10 if
   *    system is waiting for a response.
   */
  int setVolume(bool relative, float volumeTo);

  /**
   * Sets mute/unmute
   * \param[in] newMute
   *    Set it to true for mute, false for unmute.
   *    Ignored if \ref toggle is set.
   * \param[in] toggle
   *    Unmute if currently muted, mute if currently unmuted
   * 
   * \return 
   *    0 on success, -1 if TCP channel is not open, -2 if protobuf encoding
   *    failed, -3 if TCP channel didn't accept the whole message, -10 if
   *    system is waiting for a response.
   */
  int setMute(bool newMute, bool toggle);

};

