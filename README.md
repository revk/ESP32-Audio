## Amazon

This part if for Amazon - as this is the *manufacturers web site* and so used as proof of title and list price. It is also links to where you can buy.

|ASIN|List price|Title|
|-----|----|----|
|[B0DQH9SK8K](https://www.amazon.co.uk/dp/B0DQH9SK8K)|£35.00|Small ESP32-S3 high quality voice recorder board with stereo TDK ICS 43434 microphjones, micro SD card slot, and charger circuit|

## Trademark

This is an open source project, but bear in mind you cannot sell boards bearing the Andrews & Arnold Ltd name, the A&A logo, the registered trademark AJK logo, or the GS1 allocated EANs assigned to Andrews & Arnold Ltd.

# Open†Mic

(† Open source)

This project provides software and PCB designs for working with I2S microphone and speaker, most notably a self contained battery operated audio recorder to micro SD card.

The software provides working for a number of different use cases.

The hardware is an audio recorder, but the [LED](https://github.com/revk/ESP32-LED) project also has audio hardware with microphones and speakers.

## Why use this?

There are loads of microphones and dictaphones at sensible prices, so why use this?

The main reason is flexibility - it is open source - the hardware can do a lot, and it already allows record to SD and upload by web post. It can eventually be a Bluetooth microphone, or a telephone or intercom, etc. You get to do what you like, and can make suggestions and pull requests and load your own code.

The other feature, which is becoming rarer these days, is that it is *stand alone*. No subscription. No accounts or sign up. No cloud services. Nobody training AI on your data.

## Button

Button works with short or long press (long is 3s).

* press to answer or end incoming SIP call
* press to start or stop recording
* hold (1s) to reject incoming SIP call
* hold (1s) to make a call
* hold (3s) to enter power off (will upload files at this point if option selected) - there is also an auto power off feature
* press to power back on

## Microphone

The software is designed to work with I2S audio input, and is coded and tested for two specific microphones. Others may work, but I2S has a number of options - so ask me if you need some other settings.

* [TDK T3902](https://www.lcsc.com/datasheet/lcsc_datasheet_2401291125_TDK-InvenSense-MMICT390200012_C3171752.pdf) is an I2S PDM microphone (so 16 bits per sample)
* [TDK ICS-43434](https://www.lcsc.com/datasheet/lcsc_datasheet_2312010321_TDK-InvenSense-ICS-43434_C5656610.pdf) is an I2S microphone (24 bits) with sample rates up to 51.6kHz

The software can work with one or two microphones (i.e. mono or stereo). When two microphones, they are the same type, on the same bus, with one wired as *left* and one as *right* channel. 

## Speaker

For I2S output the [MAX98357A](https://www.lcsc.com/datasheet/lcsc_datasheet_2106070436_Analog-Devices-Inc--Maxim-Integrated-MAX98357AEWL-T_C2682619.pdf) is recommended. This can be one chip working as *left*, *right*, or *left+right* channels, or as two chips on the same bus, one as *left* and one as *right*.

* `spkrate` sets the speaker rate, which (for the MAX98357A) can be 8kHz, 16kHz, 32kHz, 44.1kHz, 48kHz, 88.2kHz, or 96kHz.

## Input processing

The input from the I2S microphones can be mono or stereo, and can be 16 or 24 bits. If a PDM microphone is used it is always 16 bits. If the ICS-43434 microphone is used it is received as 24 bits which can either be passed on unprocessed, or converted to 16 bits.

* `micws` is the GPIO for the channel, if not set it is assumed to be PDM mode
* `micrate` sets the sample rate
* `micstereo` is set for stereo working
* `micright` is set for *right* channel when mono.
* `micgain` sets the gain for processing 24 bits to 16

When `micgain` is set to `0`, or when using a PDM microphone, the samples (stereo or mono) are used as received, with no processing. This means for PDM mode this is 16 bits per sample, and for ICS-43434 it is 24 bits per sample.

When `micgain` is not `0`, for ICS-43434, the 24 bit samples are processed by multiplying by `micgain/256` and clipping to 16 bits (`-32768` to `32767`). A `micgain` of `8` works well for speech.

## Output

The output via the speaker can be one of the following - normally managed via MQTT control messages or web interface, though SIP is automatic when a call arrives.

* Morse code (including prosigns in `[]`, e.g. `[INT]`) and punctuation
* DTMF
* Playing a WAV file from SD card (TODO)
* SIP (see below)

The format will depend on the source - the MAX98357A can handle 16, 24, or 32 bits per channel.

## Audio recorder

<img src=https://github.com/user-attachments/assets/b2708de2-0341-4d64-8e58-601e501d9787 width=25% align=right>

The main use of this software is as an *audio recorder*. This will record from the microphone(s) to a WAV file on the micro SD card. It allows a `button` GPIO to start/stop recording and power on/off. It allows for a separate WS2812 record status LED. The PCB design includes LiPo charging.

Options are planned:-

* Stream audio over WiFI by some means
* Stream audio over bluetooth, if ever I work out how to do that

An option exists to upload files to a web page as a `POST`, renaming the file slightly once uploaded so not uploaded twice. This is done when you hold the button to *power off`*.

An option exists to delete old files if space needed for new file.

## SIP working

The software allows settings `siphost`, `sipuser`, and `sippass`, such that the device registers with a SIP server.

Once registered it can then accept a call, and pass audio both ways over RTP, 8kHz alaw. Only one call at a time.

* The microphone is changed to mono (channel selected by `micright`) 8kHz, and either 16 bit PDM, or processed 24 bit to 16 bit using `micgain`, and then companded to alaw.
* The speaker is sent dual mono audio expanded from 8kHz alaw to 16 bits per sample.

Note that there is no echo cancellation, so a caller is likely to hear their own audio come back to them.

Once the call is complete, the audio processing reverts to previous settings.
