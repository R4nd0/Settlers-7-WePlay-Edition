`puff.c` and `puff.h` are unmodified puff 2.3 sources from zlib 1.3.1,
Copyright 2002–2013 Mark Adler. The original license is retained in `puff.h`.

Upstream:
- https://github.com/madler/zlib/blob/v1.3.1/contrib/puff/puff.c
- https://github.com/madler/zlib/blob/v1.3.1/contrib/puff/puff.h

Original download SHA256:
- puff.c: `5b9d75aeb5baf3575415bc6ade3f2a02e50b6b971b3f8b4fda2b03543bc6e52f`
- puff.h: `969b7be2a930db0cdcb19b0e5b29ae6741f5a8f663b6dba6d647e12ec60cfa8e`

Only the opt-in NEO-Logistics build links this decoder. The saved-cache reader
provides a bounded output buffer and checks the complete gzip and archive data
CRCs; it does not inflate the native world. There is no additional runtime DLL.
