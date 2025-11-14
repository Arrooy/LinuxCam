Todo:

## Quick Fixes (Low Time/Complexity)

1. ✅ Fix: In options menu, Media browser should be renamed to Media settings.

2. ✅ Fix: Double click on a layer should select and open the Media settings for that layer.

3. ✅ Fix: when we dont select any device, or unselect the only selected device. the ui freezes since we dont get frames, we dont render.

## Medium Features (Medium Time/Complexity)

0. ✅ fix profiling issues. We are replicating nodes. For 50 frames, we replicate lots of nodes > 1000.

1. Add a menu option to configure face detector.
    Be able to adjust the threshold.
    Toggle to display or not a rectangle arround each face.

2. Output image cannot be resized. I would like to be able to define a size in the media settings, and aply a resize for all next frames that come. I must be able to define the
    resizing algorithm from a combobox.

3. Add a menu option for faceswap config.
    User can select a face (image) from a folder. This will be the target.
    User can change between faces. We dont change face untill next face is preprocessed and ready (arcface done...)
    Limit the amount of faces we swap

4. Different resolutions result in different face swaps.


5. Websockets enhancements
 Dynamic JPEG quality based on network conditions
 Per-client encoding (different resolutions)
 H.264/WebRTC for lower latency
 Bandwidth monitoring and adaptive bitrate

## Major Features (High Time/Complexity)

SIM SWAP IS NEXT!

Upgrade landmark-based alignment for face swaping.

Keep SCRFD for detection.

Replace the 5-point alignment with 68-point (dlib) or 106-point (InsightFace) landmarks.

Instead of affine with 5 points, use:

Affine with more points (e.g. least-squares fit from many landmarks).

Or better: non-rigid warping (Thin Plate Spline) so the mouth/jaw can be warped separately from eyes.

👉 This reduces unnatural stretching in open-mouth cases.
