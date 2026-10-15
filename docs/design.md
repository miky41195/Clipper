# Clipper — Design Spec

Data: 2026-10-16
Stato: approvato in chat, in revisione scritta

## 1. Obiettivo

Clipper è un registratore di clip per Windows 11, leggero e con UI moderna. Cattura desktop o gioco (video + audio) in due modalità:

- **Replay buffer**: tiene sempre in memoria gli ultimi N secondi/minuti (N configurabile). Una hotkey li salva come clip.
- **Registrazione manuale**: una hotkey avvia e ferma la registrazione.

L'audio è catturato **per singola app**, una traccia per app più il microfono. In galleria si scelgono le tracce da tenere (es. togliere la musica dimenticata accesa).

### Criteri di successo

| Metrica | Obiettivo |
|---|---|
| CPU in cattura 1440p60 | ≤ 2% |
| Impatto FPS gioco | ≤ 1–2% |
| RAM a riposo (UI chiusa, buffer escluso) | ≤ 40 MB |
| RAM con UI aperta | ≤ 120 MB |
| Tempo di salvataggio di una clip da 60 s | < 1 s |
| Desync audio/video | < 20 ms |

Hardware di riferimento: RTX 4070 SUPER (NVENC con H.264/HEVC/AV1).

### Fuori scope v1

Editor di taglio, upload sui social, overlay webcam, cattura multi-monitor simultanea, push-to-talk/mute del microfono, ottimizzazioni specifiche AMD/Intel (resta solo il fallback generico Media Foundation), reverse engineering di prodotti terzi.

## 2. Stack

- C++20, CMake, MSVC. Solo Windows 11 x64.
- Cattura video: Windows.Graphics.Capture (WGC) su D3D11 (C++/WinRT).
- Encoding video: encoder MFT hardware di Media Foundation (NVENC), input texture D3D11 tramite `IMFDXGIDeviceManager`. I frame non passano mai dalla RAM di sistema.
- Audio: WASAPI. Process loopback (`ActivateAudioInterfaceAsync` + `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`) per ogni app; capture standard per il microfono.
- Encoding audio: AAC tramite MFT Media Foundation (48 kHz stereo, 160 kbps per traccia, configurabile).
- Mux/demux: libavformat/libavcodec (solo librerie, linkate dinamicamente; niente ffmpeg.exe).
- UI: Dear ImGui (backend Win32 + D3D11), font Inter, icone Lucide, tema scuro custom.
- Impostazioni: JSON (nlohmann/json) in `%APPDATA%\Clipper\settings.json`.
- Test: GoogleTest + ffprobe per le verifiche di integrazione.

## 3. Architettura

Processo unico `clipper.exe`. Moduli indipendenti con interfacce minime:

```
CaptureVideo ──texture──▶ VideoEncoder ──pacchetti──┐
CaptureAudio ──PCM/app──▶ AudioEncoder ──pacchetti──┼──▶ ReplayBuffer ──snapshot──▶ ClipWriter ──▶ MP4/MKV
                                                    └──▶ Recorder (modalità rec, mux diretto)
Hotkeys ─▶ Controller ◀─▶ Settings
              │
              ├─▶ Tray + Notify (toast overlay)
              └─▶ UI on-demand (Impostazioni, Galleria, Player)
```

### 3.1 Orologio comune
Tutti i timestamp (video e audio) sono in unità QPC convertite a 100 ns, con origine all'avvio della sessione di cattura. La sincronizzazione A/V dipende solo da questo.

### 3.2 CaptureVideo
- Input: indice monitor, FPS target.
- Crea una `GraphicsCaptureSession` sul monitor scelto e consegna `ID3D11Texture2D` + timestamp. Limita gli FPS scartando i frame in eccesso; in assenza di nuovi frame ripete l'ultimo (desktop statico).
- Lo scaling alla risoluzione di output avviene in GPU (`ID3D11VideoProcessor`), con conversione a NV12.
- Bordo giallo WGC disattivato (`IsBorderRequired = false`), cursore configurabile.
- Al cambio di risoluzione o alla perdita del monitor: ricrea la sessione e segnala una discontinuità.

### 3.3 VideoEncoder
- Input: texture NV12 + timestamp. Output: pacchetti codificati con flag keyframe.
- Codec: AV1 / HEVC / H.264; ordine di fallback: codec scelto → HEVC → H.264 HW → H.264 SW (con avviso).
- Rate control: CBR/VBR con il bitrate del preset. GOP = 1 s (un keyframe al secondo), niente B-frame (latenza minima, snapshot semplice).
- Preset di qualità (bitrate a 1080p60; scalato linearmente per pixel×fps, AV1/HEVC −30% rispetto a H.264):
  - Bassa 8 Mbps, Media 15 Mbps, Alta 25 Mbps, Custom.

### 3.4 CaptureAudio
- `SessionWatcher`: enumera le sessioni audio del dispositivo di output (`IAudioSessionManager2`) e riceve le notifiche di nuove sessioni. Ogni PID che produce audio e non è in blacklist ottiene un `ProcessLoopbackSource`.
- La traccia è per **eseguibile** (PID raggruppati per nome exe), così ad esempio i processi multipli di Chrome finiscono su una traccia sola. Nome traccia = nome visualizzato dell'app; l'icona viene estratta dall'exe per la UI.
- Microfono: dispositivo scelto (default di sistema), sempre registrato, traccia `Microfono`.
- Opzione "traccia mix": traccia 1 = mix di tutte le tracce, per compatibilità con i player che leggono solo la prima.
- Tutto il PCM viene convertito a 48 kHz stereo float, con timestamp sull'orologio comune.

### 3.5 AudioEncoder
Un encoder AAC per traccia. Nei periodi in cui una traccia esiste ma è muta, emette frame di silenzio, così ogni traccia copre la clip senza buchi.

### 3.6 ReplayBuffer (ibrido RAM/disco)
- Input: pacchetti video e audio con timestamp. Indice dei keyframe video.
- Parametri: `durata` (10 s – 20 min), `sogliaRAM` (default 2 min).
- Se `durata ≤ sogliaRAM`: ring buffer in RAM. I pacchetti più vecchi di `durata + 1 s` vengono scartati per GOP intero.
- Se `durata > sogliaRAM`: i pacchetti vengono scritti in segmenti da 10 s (formato interno: header + pacchetti raw + indice) nella cartella temp (default `%LOCALAPPDATA%\Clipper\buffer`). Il segmento corrente resta in RAM e i segmenti più vecchi di `durata` vengono cancellati. La RAM resta costante (~1 segmento).
- `snapshot(secondi)`: restituisce un iteratore sui pacchetti dal keyframe ≤ (ora − secondi) fino a ora, includendo i pacchetti audio della stessa finestra e i metadati delle tracce (nome app, intervalli di attività). Non copia dati: sui pacchetti in RAM usa riferimenti a buffer con conteggio, sui segmenti su disco lettura a stream.
- Cambio impostazioni di durata/soglia a caldo: il buffer viene svuotato e ricreato (con avviso nella UI).

### 3.7 ClipWriter
- Riceve lo snapshot e scrive MP4 (default) o MKV con libavformat: 1 traccia video + N tracce audio, con metadato `title` = nome app/Microfono/Mix.
- Scrive su `<nome>.tmp` e poi rinomina. MP4 con `faststart`.
- Gira su un thread worker: la cattura non si ferma mai durante il salvataggio.

### 3.8 Recorder
Modalità registrazione manuale: si iscrive allo stesso flusso di pacchetti dell'encoder e muxa direttamente su file (MKV interno per resistere ai crash, rimuxato a MP4 alla fine se l'output scelto è MP4). Il replay buffer continua a funzionare in parallelo. Le tracce audio sono fissate all'avvio della registrazione: le app che compaiono durante la registrazione vanno nella traccia `Altre app`.

### 3.9 Hotkeys
`RegisterHotKey` globale. Azioni: Salva clip (default `Alt+F10`), Avvia/ferma registrazione (default `Alt+F9`), Apri galleria (default `Alt+G`). Se la registrazione fallisce (combinazione già occupata) l'errore viene mostrato in Impostazioni e la combinazione non viene salvata.

### 3.10 Controller
Macchina a stati: `Idle` → `Buffering` ⇄ `Buffering+Recording`; `Paused`. Riceve i comandi da hotkey, tray e UI; gestisce errori e fallback; pubblica gli eventi verso tray e toast.

### 3.11 Tray + Notify
- Icona con stato (verde = buffer attivo, rosso = registrazione, grigio = pausa).
- Menu: Salva clip · Avvia/ferma registrazione · Pausa buffer · Apri galleria · Impostazioni · Esci.
- Toast overlay: finestra layered top-most, click-through, che non prende il focus; angolo configurabile; 3 s. Testo es. "Clip salvata · 60 s · 287 MB". Suono opzionale. Toast Windows come fallback.

### 3.12 UI (on-demand)
Finestra ImGui creata all'apertura e distrutta alla chiusura (libera device D3D e risorse). Stile: tema scuro, accento configurabile, angoli arrotondati, Inter, icone Lucide, transizioni morbide.

**Impostazioni** (sidebar):
1. Cattura: monitor, risoluzione di output (nativa/1440p/1080p/720p), FPS (30/60/120/144), codec, qualità/bitrate, cursore sì/no, **stima live** (vedi 3.13).
2. Replay: durata (slider 10 s – 20 min), soglia RAM→disco, cartella temp.
3. Audio: app rilevate con toggle cattura/escludi (blacklist persistente), microfono, traccia mix sì/no.
4. Hotkey: campi "premi combinazione" con rilevamento dei conflitti.
5. Output: cartella clip (default `Video\Clipper`), MP4/MKV, schema del nome (`{app}`, `{data}`, `{ora}`; `{app}` = exe in primo piano al momento del salvataggio).
6. Generale: avvio con Windows (spento di default), suoni, lingua IT/EN.

**Galleria**: griglia di miniature (prima keyframe, cache in `%LOCALAPPDATA%\Clipper\thumbs`), raggruppamento per data/app, durata e dimensione, ricerca, rinomina, elimina (nel Cestino), apri cartella, copia file negli appunti.

**Player**: decodifica con Media Foundation Source Reader (video su texture D3D11), seek, pannello tracce (icona + nome app, checkbox, slider volume) applicato in tempo reale tramite un mixer interno su WASAPI. **Esporta**: nuova clip `<nome>_export` con il video copiato e una traccia audio ricodificata dal mix delle tracce scelte (più, opzionalmente, le tracce scelte separate).

### 3.13 Stima dimensione
`dimensione ≈ (bitrate_video + n_tracce × bitrate_audio) × durata / 8`, con `n_tracce` stimato come app attualmente attive + microfono (+ mix). Mostrata come:
- "≈ X MB per clip di N s" e "≈ Y MB/min in registrazione";
- consumo buffer: "RAM ≈ X MB" oppure "Disco temp ≈ Y GB (RAM ≈ 1 segmento)".

## 4. Gestione errori

| Caso | Comportamento |
|---|---|
| Encoder HW assente/occupato | Fallback secondo 3.3, avviso in tray |
| Monitor scollegato / cambio risoluzione | Ricreazione della sessione, discontinuità con keyframe nel buffer |
| App audio si chiude/apre | Traccia chiusa/aggiunta al volo; silenzio nei periodi di assenza |
| Disco temp pieno o scrittura troppo lenta | Buffer ridotto a RAM con la durata massima possibile, avviso |
| Hotkey in conflitto | Errore in Impostazioni, combinazione non salvata |
| Crash | Log in `%LOCALAPPDATA%\Clipper\logs`; all'avvio pulizia dei segmenti temp e dei `.tmp` orfani |
| Contenuto DRM | Appare nero (limite di WGC), documentato |
| Salvataggio durante un salvataggio | Messo in coda |

## 5. Test

- **Unit (GoogleTest)**: ReplayBuffer (rotazione RAM, passaggio a disco, snapshot che parte dal keyframe corretto, durate limite, cancellazione dei segmenti), stima dimensione, (de)serializzazione delle impostazioni, validazione e parsing delle hotkey, raggruppamento delle sessioni audio per exe.
- **Integrazione**: sorgenti sintetiche (frame con lampo bianco + tono audio allineati) → encoder → buffer → ClipWriter. Verifica con ffprobe di durata, numero e nomi delle tracce, desync < 20 ms tra lampo e beep. Export dal player con le tracce filtrate.
- **Benchmark**: `clipper --bench 600` registra 10 minuti e riporta CPU media, RAM picco, dimensione dei file e tempo di salvataggio; i risultati vanno confrontati con la tabella al §1.

## 6. Struttura repo

```
clipper/
  CMakeLists.txt
  src/
    app/          main, Controller, Settings
    capture/      CaptureVideo, CaptureAudio, SessionWatcher
    encode/       VideoEncoder, AudioEncoder
    buffer/       ReplayBuffer, Segment
    output/       ClipWriter, Recorder, Exporter
    platform/     Hotkeys, Tray, Toast, Clock
    ui/           Theme, SettingsView, GalleryView, PlayerView, Mixer
  third_party/    imgui, nlohmann_json, ffmpeg (libs), lucide font
  tests/
  docs/
```
