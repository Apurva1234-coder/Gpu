Place locally downloaded instances under the family/type folders. Supported benchmark formats: project JSON/text, linear MPS (including MPS `RANGES`, `OBJSENSE`, and integer/binary bound records), and gzip-compressed MPS such as MIPLIB `.mps.gz`. Gzip decompression is streamed to a temporary file that is removed after each solver invocation. The runner does not download datasets. Generated result artifacts belong in results/.

Netlib's `.mps.txt` EMPS compression is detected and reported UNSUPPORTED. A previous experimental decoder did not preserve AFIRO constraint coefficients, so that conversion path is disabled to prevent false successful results. Standard QPLIB `.qplib`, quadratic MPS, and other unsupported formats are likewise reported UNSUPPORTED. No lossy conversion is used.

`netlib/small/afiro.mps` is a local copy of the repository fixture `examples/afiro.mps`; the workspace also contains Netlib EMPS files and gzip MIPLIB MPS inputs. No `.qplib` instance files or compatible Mittelmann inputs are present. Generated output remains separate in `results/`.

enchmarks/examples/small contains local copies of the project LP/MILP smoke fixtures used for Tier 1 infrastructure verification.
