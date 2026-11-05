# Credits

## Eisenmonoxid — thank you ❤️

Warm greetings, a huge thank-you and lots of love to
[Eisenmonoxid](https://github.com/Eisenmonoxid). Your excellent groundwork made
this project possible. Thank you for sharing the tools and helping the community
keep The Settlers 7 alive.

- [S7Patcher](https://github.com/Eisenmonoxid/S7Patcher): the upstream patcher,
  executable definitions and the starting point for this fork.
- [S7Packer](https://github.com/Eisenmonoxid/S7Packer): the archive-format and
  cryptography implementation followed by our archive tools. Its MIT license
  is included at [tools/lan/LICENSE.S7Packer.txt](tools/lan/LICENSE.S7Packer.txt).

Upstream copyright and MIT terms remain in [LICENSE](LICENSE). WePlay additions
are maintained by R4nd0 and community contributors.

## Graphics and runtimes

Optional graphics translations and bundled runtimes retain their own licenses
and notices in the installer/package. DXVK is developed by
[the DXVK contributors](https://github.com/doitsujin/dxvk).

## NEO save reader

The bounded NEO checkpoint reader uses Mark Adler's upstream **puff 2.3**, from
zlib 1.3.1. Its copyright and permissive zlib terms remain in
[puff.h](native/third_party/puff/puff.h). NEO packages include that notice as
`Package/Tools/Licenses/NEO-Logistics-puff.txt`, alongside
`NEO-Logistics-S7Packer.txt`. The release ZIP also supplies both notices in its
`Licenses` directory. No game archives or saves are distributed with the reader.

## Branding

The WePlay artwork is generated community artwork. Asset provenance is recorded
in [assets/branding/ASSETS.txt](assets/branding/ASSETS.txt). Game names and
trademarks remain the property of their owners. This project is independent.
