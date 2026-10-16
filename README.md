# Clipper

Registratore di clip leggero per Windows 11. Tiene sempre pronti gli ultimi N secondi dello schermo (replay buffer) e li salva con una scorciatoia; può anche registrare manualmente. Ogni clip ha l'audio su tracce separate: Mix, Microfono e una traccia per ogni app che sta suonando. Nella galleria puoi spegnere o abbassare qualunque traccia ed esportare la clip senza ricodificare il video.

## Uso

- Avvia `build\bin\clipper.exe`: compare un pallino in tray (verde = buffer attivo, rosso = registrazione, grigio = pausa). `clipper.exe --show` apre subito la finestra; se Clipper è già in esecuzione, rilanciarlo apre la finestra.
- **Alt+F10**: salva gli ultimi secondi.
- **Alt+Shift+F9**: avvia / ferma la registrazione.
- **Alt+G**: apre la galleria.
- Clic sull'icona in tray: salva, registra, pausa, galleria, impostazioni, cartella, esci.

## Finestra

- **Galleria**: clip raggruppate per giorno, ricerca per nome o gioco. Tasto destro su una clip: rinomina, mostra nella cartella, copia il file negli appunti (da incollare su Discord), sposta nel Cestino.
- **Player**: clic sul video o Spazio per play/pausa, frecce ←/→ per ±5 s. Il pannello "Tracce audio" spegne o regola ogni traccia mentre ascolti. La traccia Mix parte spenta perché duplica le altre.
- **Esporta**: crea `<nome>_export.mp4` con il video copiato senza perdita e le tracce scelte mixate in una sola; opzionalmente tiene anche le tracce separate.
- **Impostazioni**: cattura (monitor, risoluzione, fps, codec, qualità), replay (durata, soglia RAM→disco), audio (microfono, app da non registrare mai), scorciatoie (clicca e premi), salvataggio (cartella, formato, nome file), generale (avvio con Windows, suoni, lingua, colore). La card **Stima** mostra in tempo reale quanto peseranno le clip e il buffer. "Applica" riavvia la cattura solo se serve.

Le impostazioni sono anche in `%APPDATA%\Clipper\settings.json`.

## Build e test

```
build.cmd                                   :: build
build\bin\clipper_tests.exe                 :: tutti i test (GPU, schermo; nessun suono)
build\bin\clipper_tests.exe -tse=gpu,capture,desktop   :: solo test unitari
set CLIPPER_AUDIO_TESTS=1                   :: abilita il test di cattura audio (riproduce un suono!)
build\bin\clipper.exe --bench 30            :: misura CPU/RAM e salva una clip di prova
build\bin\clipper.exe --render-ui settings.capture out.png 1.5   :: anteprima di una pagina
```

Log: `%LOCALAPPDATA%\Clipper\logs\clipper.log`. Miniature e indice della galleria: `%LOCALAPPDATA%\Clipper\thumbs`.
