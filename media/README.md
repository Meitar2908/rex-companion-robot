# Media checklist

Drop files here with these exact names and the main README picks them up
automatically — no need to edit any links.

## `photos/`

Have: `hero.jpg`, `idle-angle.jpg`, `expression-sleepy.jpg`, `from-behind.jpg`,
`internals.jpg`.

Still worth adding:

| Filename | Shot |
|---|---|
| `expression-2.jpg`, `expression-3.jpg` | Close-ups of two more *different* expressions (e.g. happy, skeptical, evil) — everything so far is the idle/sleepy face, so these would show the actual animation range. Say `/emotion <name>` over serial, or just ask REX to make a face, and shoot the screen. |
| `case-angle.jpg` | 3/4 view of the enclosure from the *outside*, showing the exterior USB-C port — `from-behind.jpg` currently covers the back but not this. |
| `scale.jpg` | REX next to something everyday (a hand, a mug, a laptop) so viewers can tell how big it actually is. |

## `video/`

| Filename | Use |
|---|---|
| `demo.gif` | A short (10-20s) looping clip — wake word → a question → REX answering with the mouth animating — converted to GIF so it autoplays inline in the README. See conversion command below. |

For a longer demo (wake word → conversation → a tool call like weather →
a sarcastic aside → idle behavior), upload it to YouTube and link it from
the README instead of committing a video file — GitHub doesn't render
video inline in markdown, and large files don't belong in git.

### Converting a phone clip to a README-sized GIF

```sh
ffmpeg -i demo.mov -vf "fps=12,scale=480:-1:flags=lanczos" -c:v gif demo.gif
```

Keep it under ~10MB — a long or high-res GIF makes the README slow to load.
Trim the source clip first (`-ss 00:00:03 -t 15`) if it's longer than needed.
