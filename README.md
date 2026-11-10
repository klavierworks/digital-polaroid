# Picture Frame

A standalone slideshow for the **Adafruit Qualia ESP32-S3** with the **TL040WVS03
480×480** panel, the same hardware as the [qualia bridge](../qualiabridge). Once
flashed it needs only power: it joins WiFi, reads an iCloud shared album, picks up
to 20 of its photos at random, and cycles through them.

## The slideshow

Each photo is held for `kHoldMs`, then the next one replaces it in a straight cut.
At boot the panel sits at plain white until the first photo arrives. If the next
photo isn't ready when the hold ends (slow network, WiFi down), the current one
stays up until it is; a failed download is retried every `kRetryMs`.

## Buttons

Up and down step the backlight by `kBrightnessStep`, between `kMinBrightness` and
full, easing over `kBrightnessFadeMs`. They sit on the XCA9554 I/O expander
(`PCA_BUTTON_UP`, `PCA_BUTTON_DOWN`) and are polled every `kButtonPollMs`; each
press counts once. The level resets to `kBrightness` on power-up.

Holding up and down together skips to the next photo, as soon as it has downloaded.
A single press waits `kChordMs` before counting, so the second button of a skip
arriving a moment late doesn't also change the brightness. Reset is wired to the
ESP32's enable line and restarts the board, so firmware cannot read it.

## Constants

Everything is hardcoded in [src/constants.h](src/constants.h). The defaults match the
brain's polaroid defaults.

| Constant | Default | Meaning |
|----------|---------|---------|
| `kHoldMs` | `60000` | how long each photo stays up |
| `kBrightness` | `120` | backlight level at power-up |
| `kBrightnessStep` | `16` | change per up/down press |
| `kMinBrightness` | `8` | lowest level the down button reaches |
| `kBrightnessFadeMs` | `120` | ease between levels on a press |
| `kButtonPollMs` | `20` | how often the buttons are read |
| `kChordMs` | `80` | how long a single press waits for the other button before counting |
| `kAlbumPhotos` | `20` | photos picked from the album per read |
| `kAlbumRefreshMs` | `5 min` | how often the album is read again (and a new set picked) |
| `kMaxDownloadBytes` | `3 MB` | largest download accepted, photo or album list |
| `kFetchTimeoutMs` | `15000` | connect timeout, reply wait, and longest gap in a transfer |
| `kRetryMs` | `10000` | wait before retrying a failed download |

Panel timings and pins are in [src/config.h](src/config.h), unchanged from the bridge.

## Secrets

Copy [src/secrets.example.h](src/secrets.example.h) to `src/secrets.h` and fill it
in. That file is gitignored and holds everything that shouldn't be committed:

| Constant | Meaning |
|----------|---------|
| `kWifiSsid` | WiFi network name |
| `kWifiPassword` | WiFi password |
| `kAlbumToken` | the part after `#` in the album's public link; anyone with it can view the album |

## WiFi

The board joins the network in `src/secrets.h` and reconnects on its own if it
drops. HTTPS certificates are not checked (`setInsecure`), which keeps the firmware
free of a certificate bundle that would expire over the frame's lifetime.

## iCloud shared album

The album needs **Public Website** turned on in its sharing settings. There is no
official API; this follows the same undocumented web endpoints as
[ICloud-Shared-Album](https://github.com/ghostops/ICloud-Shared-Album):

1. The token's second and third characters (only the second if it starts with `A`)
   are a base-62 server partition, giving
   `https://pNN-sharedstreams.icloud.com/<token>/sharedstreams/`.
2. `POST webstream` with `{"streamCtag":null}` lists the album. Apple may answer
   status 330 with `{"X-Apple-MMe-Host": ...}`, the album's real host; the request is
   repeated there.
3. Videos are ignored. Each photo has two derivatives, a thumbnail (about 342 px)
   and the shared size (about 1536×2048); the larger one is used.
4. `POST webasseturls` with `{"photoGuids":[...]}` returns a download URL per
   derivative. These expire three hours after they are issued, so the album is read
   again every two hours, and also whenever a photo download is refused.

Apple builds the whole photo list before replying, which took 40–50 s for a
2,400-photo album; small albums answer in well under a second and fit the normal
15 s wait.

## Downloading

Requests are made directly over a TLS socket as HTTP/1.0 rather than through
`HTTPClient`, whose body reader pulls one byte at a time and stalled on these
downloads, and whose reply wait tops out at 65.5 s. HTTP/1.0 keeps replies
unchunked and the body is read in bulk into PSRAM. GET redirects are followed up to
three times.

## Decoding

The shared-size photos are baseline JPEGs of about 1536×2048, too large to decode at
full size into PSRAM. [JPEGDEC](https://github.com/bitbank2/JPEGDEC) decodes them at
the largest reduction (½, ¼ or ⅛) that keeps the short edge at least 480 px, which
is ½ for these, and the result is scaled with bilinear filtering to cover 480×480
and centre-cropped. Progressive JPEGs are skipped, as JPEGDEC only decodes their
first low-resolution scan. The resize pauses every 16 rows so its PSRAM traffic
never starves the display's scan-out.

Download and decode run in their own FreeRTOS task, so the fades keep stepping
while the next photo is prepared. Each download logs its size and how long the
download and decode took.

## Build and flash

```bash
pio run -t upload
pio device monitor
```

## Layout

| File | Concern |
|------|---------|
| [src/main.cpp](src/main.cpp) | setup/loop |
| [src/slideshow.cpp](src/slideshow.cpp) | hold and switch sequencing |
| [src/inputs/fetcher.cpp](src/inputs/fetcher.cpp) | background task handing decoded photos to the slideshow |
| [src/inputs/album.cpp](src/inputs/album.cpp) | iCloud shared album: photo list, random pick, download URLs |
| [src/inputs/download.cpp](src/inputs/download.cpp) | HTTPS GET/POST into a PSRAM buffer |
| [src/inputs/decode.cpp](src/inputs/decode.cpp) | scaled JPEG decode, resized and cropped to the panel |
| [src/inputs/buttons.cpp](src/inputs/buttons.cpp) | up/down presses and the both-buttons skip, from the I/O expander |
| [src/inputs/wifi.cpp](src/inputs/wifi.cpp) | joining the network |
| [src/outputs/](src/outputs) | panel, scan-out and fades, copied from the bridge |
