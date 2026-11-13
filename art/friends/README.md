# Raiden's friends

Two more props in the Saloon, like Raiden: not villagers, no friendship, gifts or romance.

- **aleas** (lowercase) plays at his own arcade cabinet next to the others, and has the next
  11 games paid for.
- **Siepert** sits at her own table typing on her laptop.

`Portraits_*.png` are 64x64 portraits from their reference sheets (0/2: busy, 1/3: looking at
you). The app ships them as `romfs/tex/Portraits_*.s3t` (rebuild with `convert_texture` in
tools/convert.py).

Their walking sprites aren't stored here: the converter makes them from the player's own
villager sheets (aleas from Shane's, recoloured to his green hoodie; Siepert from Emily's,
with a black dress, a white apron and cat ears). The cabinet and the table are copied from the
Saloon map's own tiles when you walk in (`lastMapFixes`).
