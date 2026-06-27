"""Minimaler PCM16-mono-Resampler (lineare Interpolation), stdlib-only.
Nötig, weil neopuck mit 16 kHz arbeitet, OpenAI Realtime mit 24 kHz, und das
`audioop`-Modul ab Python 3.13 entfernt wurde. Für Sprache völlig ausreichend."""
import array


def resample(data: bytes, src_sr: int, dst_sr: int) -> bytes:
    if src_sr == dst_sr or not data:
        return data
    s = array.array("h")          # int16, native (little-endian auf macOS/x86/arm)
    s.frombytes(data)
    n = len(s)
    if n == 0:
        return b""
    out_n = max(1, round(n * dst_sr / src_sr))
    out = array.array("h", bytes(2 * out_n))
    ratio = (n - 1) / (out_n - 1) if out_n > 1 else 0.0
    for i in range(out_n):
        pos = i * ratio
        i0 = int(pos)
        frac = pos - i0
        a = s[i0]
        b = s[i0 + 1] if i0 + 1 < n else a
        out[i] = int(a + (b - a) * frac)
    return out.tobytes()
