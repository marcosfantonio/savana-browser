# WhatsApp preview investigation — 2026-10-26

## Confirmed on the local installation

- WebKitGTK 2.54.2-1; GStreamer and plugin packages 1.28.8-1.
- Cores 480765, 481159 and 482976 crash at WebKit offset `0x2c48099`.
  Disassembly identifies `WebCore::configureVideoDecoderForHarnessing`.
  Its caller is `ImageDecoderGStreamer`'s parser-pad callback. The factory
  saved in core 482976 is named `nvh264dec`; factory creation returned NULL.
- Excluding `nvh264dec` prevents that particular crash in an isolated MP4-as-image
  test, but does **not** bound memory consumption for a longer preview.
- At 15:49:19 the kernel killed WebKit process 486576 with
  `CONSTRAINT_MEMCG`: the Savana cgroup reached 8,318,904,320 bytes of memory
  and 4,159,452,160 bytes of swap. This was not another SIGSEGV.
- That service used `OOMPolicy=stop`. systemd subsequently terminated other
  processes in the service. A cgroup limit alone therefore does not guarantee
  that the rest of the browser survives. The UI also had `oom_score_adj=200`;
  do not assume only WebKit has that score.

## Independent memory reproducer

No WhatsApp, upload, Web Worker, WASM or WebCodecs is needed for this reproducer.
The temporary harness uses GTK4/WebKitGTK with the sandbox left enabled and
sets an HTML Image's source to a data URL containing an H.264 MP4.

```sh
ffmpeg -hide_banner -loglevel error -f lavfi \
  -i color=c=blue:size=1920x1080:rate=30 -t 8 \
  -c:v libx264 -preset ultrafast -pix_fmt yuv420p preview.mp4
```

The generated file is only 11,453 bytes, but the isolated test reached a hard
1.5 GiB cgroup memory limit in 1.18 seconds, with swap disabled. The kernel
killed the process. Temporary diagnostic artifacts are:

- `/tmp/opencode/whatsapp-preview-test.py`
- `/tmp/opencode/whatsapp-preview-test.mp4`
- `/tmp/opencode/whatsapp-core-backtrace.txt`
- `/tmp/opencode/whatsapp-decoder-core.txt`
- `/tmp/opencode/whatsapp-decoder-name.txt`

The bounded test command was:

```sh
systemd-run --user --unit=savana-preview-memory-test-3 --wait --pipe \
  -p MemoryMax=1536M -p MemorySwapMax=0 -p OOMPolicy=stop \
  env GST_PLUGIN_FEATURE_RANK=nvh264dec:NONE \
  python3 /tmp/opencode/whatsapp-preview-test.py
```

Do not run unbounded reproductions with large videos.

## Source evidence and correction scope

In WebKit's `webkitgtk-2.54.2` tag:

- `Source/WebCore/platform/graphics/gstreamer/ImageDecoderGStreamer.cpp:147`
  creates the decoder and immediately calls
  `configureVideoDecoderForHarnessing` without a NULL check.
- `storeDecodedSample` (line 283) creates an image for each decoded sample and
  retains the samples in `m_sampleData`.
- `ImageDecoderGStreamerSample` also retains the original sample and frame.
  Memory can therefore scale with the number and resolution of decoded frames,
  not with the compressed attachment size.

This establishes a standalone WebKit image-decoding memory problem, not proof
that WhatsApp's WASM transcode is responsible for the observed OOM. Other media
paths may still consume memory; an allocation profile during the actual
WhatsApp operation would distinguish them.

A complete engine fix must handle decoder creation failure and bound/release
decoded-frame storage without breaking frame access. The decoder exclusion in
Savana addresses only the former crash trigger, not the latter memory problem.
There is no video-as-image toggle among the installed WebKit feature flags.

`OOMPolicy` could not be changed on a running service using systemd's
`SetUnitProperties` (`Cannot set property OOMPolicy`). It needs to be configured
when starting the dedicated unit (e.g. `OOMPolicy=continue`), rather than silently
adding it to the current memory guard's D-Bus property list. Reducing limits,
raising limits, or disabling the sandbox is not a fix for frame retention.
