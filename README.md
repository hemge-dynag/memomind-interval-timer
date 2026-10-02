# MemoMind — Chrono d'intervalles (interval-timer)

Application **autonome** pour lunettes MemoMind : chronomètre avec compteur de tours et contrôle par gestes de tête.

## Composition

| Côté | Dossier | Artefact |
| --- | --- | --- |
| Lunettes (plugin natif C) | `glass/interval-timer/` | `builds/interval-timer.gmp` |

Pas de composant téléphone (app autonome).

## Fonctionnement

- Chronomètre `mm:ss.cc`.
- Geste tête **haut** = démarrer/reprendre, geste tête **bas** = pause.
- Bouton : appui simple = enregistrer un tour (ou démarrer), appui long = remise à zéro.
- Affiche le temps total, l'état et le dernier tour.

## Build

Nécessite le SDK officiel MemoMind (`memomind-open/plugin-open-platform`). Déposer `glass/interval-timer/` dans `GlassSDK/examples/`, puis :

```
python3 build.py glass --force
```

→ `builds/interval-timer.gmp`

Bilingue FR/EN.
