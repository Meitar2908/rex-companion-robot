# Media checklist

Drop files here with these exact names and the main README picks them up
automatically — no need to edit any links.

## `photos/`

Have: `hero.jpg`, `expression-worried.jpg`, `expression-surprised.jpg`,
`internals.jpg`, `from-behind.jpg`, `build-printing.jpg`,
`build-breadboard.jpg`, `build-handheld.jpg`.

Still worth adding:

| Filename | Shot |
|---|---|
| `expression-happy.jpg` or similar | One more clearly *positive* expression — everything so far reads worried/surprised/neutral, so a happy or smug one would round out the range. |
| `case-angle.jpg` | 3/4 view of the enclosure from the *outside*, showing the exterior USB-C port. |
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
