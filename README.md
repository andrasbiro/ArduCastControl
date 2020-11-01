# ArduCastControl

Chromecast control library for platformio/arduino. It supports requesting some
information of the casting (e.g. artist and title), as well as a minimal control
(pause, previous, next, seek and volume).

## Dependencies

The library depends on [ArduinoJson](https://arduinojson.org/) and
[nanopb](https://jpa.kapsi.fi/nanopb/). This is already set for platformio.

It has a significant RAM footprint, which is usually not an issue for wifi
capable boards. It was only tested on ESP8266.

## References

- [Node-castv2](https://github.com/thibauts/node-castv2) has a great readme
  describing the protocol.
- [pychromecast](https://github.com/home-assistant-libs/pychromecast) was used
  as a reference when implementing this
- [CastVolumeKnob](https://github.com/WebmasterTD/CastVolumeKnob/) is a similar
  project in micropython, but it only targets volume control.
- [Google's documentation on protocol
  buffers](https://developers.google.com/protocol-buffers/docs/encoding)

## Flow of connection

Understanding how the CASTV2/chromecast protocols work took me a while, so
here's a quick summary on what this library does.

1. TCP/TLS connection is opened to the device. Self-signed certificates are
   sufficient
2. An application layer connection to the device itself ("receiver-0") is
   established
3. Status is requested from the device with GET_STATUS messages on the receiver
   namespace
    1. Reported status include volume information
4. When something casts to the device, chromecast reports an application is
   running with a sessionId
5. An application layer connection to the application (sessionid) is established
6. Status is requested from the application with GET_STATUS messages on the
   media namespace
    1. Reported status includes all sort of information of the currently playing
       track
    2. As well as a mediaSessionId, which is changed with every track change
7. Control messages can be sent to the application on the media namespace using
   a specified mediaSessionId

## Useful values saved

All of the following are public fields of the class, which are updated when the
class' loop() function is called on a given status.

- **volume** - The volume set on the device, between 0 and 1
- **isMuted** - True if the device is muted
- **displayName** - Typically the application casting, like "Spotify"
- **statusText** - A short status, e.g. "Casting: Whole Lotta Love"
- **playerState** - State of playback, e.g. "PLAYING"
- **title** - Title of the current song, e.g. "Whole Lotta Love"
- **artist** - Artist of the current song, e.g. "Led Zeppelin"
- **duration** - Duration of the current song in seconds, e.g. 333.89
- **currentTime** - Current time in the song in seconds, e.g. 2.27

This list can be easily extended by saving more when processing MEDIA_STATUS or
RECEIVER_STATUS.

## Control methods

The following controls are accessible as methods in the class:

- **play()** - Plays (resumes) the current song
- **pause()** - Pauses/Resumes the current song
- **prev()** - Previous track
- **next()** - Next track
- **seek()** - Seeks in song
- **setVolume()** - Volume control
- **setMute()** - Mute control

I think this covers all possible controls except casting and playlist features.
However, extending it should be fairly easy, using the play() or setVolume()
method as a template (for media/device commands respectively)

## Further documentation

For further documentation, please refer to the comments in ArduCastControl.h and
the example, which demonstrates the main features.

## Further developement

Don't expect any new features/bugfixes, as I'm quite happy with the featureset
as is. However, I do accept pull requests.