# Code Coverage and Static Recompilation Audit

## Executive Summary

- **Target ROM:** Kingdom Hearts: Chain of Memories (USA, B8CE)
- **Total Identified Functions:** 5162
- **Statically Recompiled Functions:** 4450
- **Orphaned Candidates:** 712
- **Coverage Metric:** 86.21%

## Top Identified Orphan Candidates

| Address | Mode | Prologue Bytes | Notes |
|---|---|---|---|
| `0x08000110` | arm | `0f402de9` | Potential unindexed helper |
| `0x080001FC` | arm | `00402de9` | Potential unindexed helper |
| `0x080101EC` | arm | `d4fa2e09` | Potential unindexed helper |
| `0x08010214` | arm | `f6d92e09` | Potential unindexed helper |
| `0x0801026C` | arm | `f6d92e09` | Potential unindexed helper |
| `0x08010298` | arm | `d4fa2e09` | Potential unindexed helper |
| `0x0805A7F0` | arm | `184c2819` | Potential unindexed helper |
| `0x0805A878` | arm | `134c2a19` | Potential unindexed helper |
| `0x0805A910` | arm | `104c2b19` | Potential unindexed helper |
| `0x0805A9E0` | arm | `2a4c2919` | Potential unindexed helper |
| `0x0805BA18` | arm | `204c2919` | Potential unindexed helper |
| `0x08066BE0` | arm | `81702109` | Potential unindexed helper |
| `0x08085FB4` | thumb | `70b54e46` | Potential unindexed helper |
| `0x080863C0` | thumb | `30b5041c` | Potential unindexed helper |
| `0x08086A34` | arm | `064c2919` | Potential unindexed helper |
| `0x08086B80` | arm | `084c2919` | Potential unindexed helper |
| `0x08086BC4` | arm | `024c2919` | Potential unindexed helper |
| `0x0808705C` | arm | `1e4c2e19` | Potential unindexed helper |
| `0x08087B98` | thumb | `f0b54746` | Potential unindexed helper |
| `0x0808B3DC` | thumb | `f0b582b0` | Potential unindexed helper |
| `0x0808B66C` | thumb | `f0b55746` | Potential unindexed helper |
| `0x0808C2F0` | thumb | `10b5041c` | Potential unindexed helper |
| `0x0808C60C` | thumb | `f0b55746` | Potential unindexed helper |
| `0x0808C8D0` | thumb | `70b5031c` | Potential unindexed helper |
| `0x0808C90C` | thumb | `30b5051c` | Potential unindexed helper |
| `0x0808C974` | thumb | `10b5041c` | Potential unindexed helper |
| `0x0808C9CC` | thumb | `30b5051c` | Potential unindexed helper |
| `0x0808CA78` | thumb | `f0b5051c` | Potential unindexed helper |
| `0x0808CAA8` | arm | `0a4c2a19` | Potential unindexed helper |
| `0x0808CB60` | thumb | `10b5041c` | Potential unindexed helper |
| `0x0808CBB4` | thumb | `70b581b0` | Potential unindexed helper |
| `0x0808CDE8` | thumb | `30b5051c` | Potential unindexed helper |
| `0x0808D0A4` | thumb | `f0b54746` | Potential unindexed helper |
| `0x0808D73C` | thumb | `30b5051c` | Potential unindexed helper |
| `0x0808D828` | thumb | `f0b54f46` | Potential unindexed helper |
| `0x0808DD20` | thumb | `f0b581b0` | Potential unindexed helper |
| `0x0808DE28` | thumb | `70b581b0` | Potential unindexed helper |
| `0x0808DED0` | thumb | `f0b55746` | Potential unindexed helper |
| `0x0808E19C` | thumb | `f0b55746` | Potential unindexed helper |
| `0x0808E344` | thumb | `10b5041c` | Potential unindexed helper |
| `0x0808E3E0` | thumb | `70b5051c` | Potential unindexed helper |
| `0x0808E474` | thumb | `f0b5041c` | Potential unindexed helper |
| `0x0808E750` | thumb | `10b5041c` | Potential unindexed helper |
| `0x0808E7D8` | thumb | `f0b54f46` | Potential unindexed helper |
| `0x0808E890` | thumb | `30b5041c` | Potential unindexed helper |
| `0x0808E8E8` | thumb | `70b50904` | Potential unindexed helper |
| `0x0808EA0C` | thumb | `f0b55746` | Potential unindexed helper |
| `0x0808F2CC` | thumb | `10b5041c` | Potential unindexed helper |
| `0x0808F304` | thumb | `10b5011c` | Potential unindexed helper |
| `0x0808F358` | thumb | `f0b5051c` | Potential unindexed helper |

## Recompiled Functions Distribution

All 4450 recompiled functions reside in `config/b8ce.toml` and are emitted into 16 C++ code shards in `generated/`.
