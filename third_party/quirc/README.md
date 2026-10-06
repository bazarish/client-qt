# quirc

Vendored QR-code reader, used by the client to read an invite off the camera.
`libqrencode` only writes QR codes; this one reads them.

Upstream: <https://github.com/dlbeer/quirc>, tag `v1.2`
(`sha256:73c12ea33d337ec38fb81218c7674f57dba7ec0570bddd5c7f7a977c0deb64c5` of
`v1.2.tar.gz`). Only `lib/` is carried: the demos and tests are not built.
Licence: ISC, see `LICENSE`.

Carried rather than taken from the distribution because the client is built on
three platforms whose packaged readers differ (Debian 12 is the AppImage base
and ships zxing-cpp 1.4, trixie ships 2.3, and their APIs are not the same).
Nothing here is patched; keep it that way, so a version bump is a copy.
