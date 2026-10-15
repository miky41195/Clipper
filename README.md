# Clipper

Registratore di clip leggero per Windows 11. Tiene in memoria gli ultimi N secondi dello schermo (replay buffer) e li salva con una scorciatoia; può anche registrare manualmente. L'audio è su tracce separate: Mix, Microfono e una traccia per ogni app che sta suonando.

## Uso

- Avvia `build\bin\clipper.exe`: compare un pallino in tray (verde = buffer attivo, rosso = registrazione, grigio = pausa).
- **Alt+F10**: salva gli ultimi secondi del buffer.
- **Alt+Shift+F9**: avvia o ferma la registrazione.
- **Alt+G**: apre la cartella delle clip (`Video\Clipper`).
- Clic sull'icona in tray: menu con pausa, impostazioni ed esci.

## Impostazioni

Il file è `%APPDATA%\Clipper\settings.json`: lo apri da tray → *Impostazioni…*; dopo averlo salvato, scegli *Ricarica impostazioni*. L'interfaccia grafica delle impostazioni arriva nel piano 2.

| Chiave | Valori |
|---|---|
| `replaySeconds` | 10–1200, durata del buffer |
| `ramThresholdSeconds` | oltre questa durata il buffer va su disco (`%LOCALAPPDATA%\Clipper\buffer`) |
| `codec` | `av1`, `hevc`, `h264` |
| `quality` / `customKbps` | `low`, `medium`, `high`, `custom` |
| `fps` | 30, 60, 120, 144 |
| `outputHeight` | 0 = nativa, 720, 1080, 1440, 2160 |
| `audioBlacklist` | es. `["spotify.exe"]`, app mai registrate |
| `mixTrack` | aggiunge la traccia Mix come prima traccia audio (compatibilità) |
| `hotkeys` | es. `"Ctrl+Shift+S"`; una stringa vuota disattiva la scorciatoia |
| `container` | `mp4` o `mkv` |
| `nameTemplate` | `{app}`, `{date}`, `{time}` |

## Build e test

```
build.cmd               :: build
build.cmd test          :: build + tutti i test (alcuni usano GPU, schermo e audio)
build\bin\clipper_tests.exe -tse=audio-device   :: senza i test che riproducono suoni
build\bin\clipper.exe --bench 30                :: misura CPU/RAM e salva una clip di prova
```

Log: `%LOCALAPPDATA%\Clipper\logs\clipper.log`.
