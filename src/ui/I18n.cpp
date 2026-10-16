#include "ui/I18n.h"
#include <atomic>
#include <string>
#include <unordered_map>

namespace clipper {
namespace {

const std::vector<I18nEntry> kTable = {
    // navigation + status
    {"nav.gallery", "Galleria", "Gallery"},
    {"nav.settings", "Impostazioni", "Settings"},
    {"status.buffering", "Buffer attivo · ultimi {}", "Buffer on · last {}"},
    {"status.recording", "Registrazione in corso", "Recording"},
    {"status.paused", "Cattura in pausa", "Capture paused"},
    {"status.off", "Cattura non attiva", "Capture off"},
    // settings sections
    {"sec.capture", "Cattura", "Capture"},
    {"sec.capture.desc", "Cosa registrare e con quale qualità", "What to record and at which quality"},
    {"sec.replay", "Replay", "Replay"},
    {"sec.replay.desc", "Quanto tenere sempre pronto da salvare", "How much recent footage to keep ready"},
    {"sec.audio", "Audio", "Audio"},
    {"sec.audio.desc", "Ogni app finisce su una traccia separata", "Every app gets its own track"},
    {"sec.hotkeys", "Scorciatoie", "Shortcuts"},
    {"sec.hotkeys.desc", "Clicca un campo e premi la combinazione", "Click a field and press the combination"},
    {"sec.output", "Salvataggio", "Output"},
    {"sec.output.desc", "Dove e come vengono salvate le clip", "Where and how clips are saved"},
    {"sec.general", "Generale", "General"},
    {"sec.general.desc", "Avvio, suoni, lingua e aspetto", "Startup, sounds, language and look"},
    // capture
    {"cap.monitor", "Monitor", "Monitor"},
    {"cap.primary", "principale", "primary"},
    {"cap.resolution", "Risoluzione", "Resolution"},
    {"cap.native", "Nativa", "Native"},
    {"cap.fps", "Frame al secondo", "Frame rate"},
    {"cap.codec", "Codec", "Codec"},
    {"cap.codec.av1", "Migliore qualità per MB", "Best quality per MB"},
    {"cap.codec.hevc", "Efficiente e compatibile", "Efficient and compatible"},
    {"cap.codec.h264", "Compatibile ovunque", "Plays everywhere"},
    {"cap.quality", "Qualità", "Quality"},
    {"q.low", "Bassa", "Low"},
    {"q.medium", "Media", "Medium"},
    {"q.high", "Alta", "High"},
    {"q.custom", "Personalizzata", "Custom"},
    {"cap.bitrate", "Bitrate video", "Video bitrate"},
    {"cap.cursor", "Mostra il cursore", "Show cursor"},
    // estimate
    {"est.title", "Stima", "Estimate"},
    {"est.clip", "≈ {} per una clip da {}", "≈ {} per {} clip"},
    {"est.rec", "≈ {} al minuto in registrazione", "≈ {} per minute while recording"},
    {"est.ram", "Buffer in RAM ≈ {}", "Buffer in RAM ≈ {}"},
    {"est.disk", "Buffer su disco ≈ {} · RAM ≈ {}", "Buffer on disk ≈ {} · RAM ≈ {}"},
    // replay
    {"rep.duration", "Durata del replay", "Replay length"},
    {"rep.threshold", "Usa il disco oltre", "Use disk above"},
    {"rep.threshold.hint", "Fino a questa durata il buffer resta in RAM: salvataggio istantaneo",
     "Up to this length the buffer stays in RAM: instant saves"},
    {"rep.tempdir", "Cartella del buffer su disco", "Disk buffer folder"},
    {"common.default", "Predefinita", "Default"},
    {"common.change", "Cambia…", "Change…"},
    {"common.open", "Apri", "Open"},
    {"common.reset", "Ripristina", "Reset"},
    // audio
    {"aud.capturemic", "Registra il microfono", "Record microphone"},
    {"aud.mic", "Microfono", "Microphone"},
    {"aud.mic.default", "Predefinito di sistema", "System default"},
    {"aud.mix", "Traccia Mix", "Mix track"},
    {"aud.mix.hint", "Prima traccia con tutto l'audio, per i player che ne leggono una sola",
     "A first track with everything, for players that read only one"},
    {"aud.bitrate", "Qualità audio", "Audio quality"},
    {"aud.apps", "App che riproducono audio", "Apps playing audio"},
    {"aud.apps.hint", "Spegni un'app per non registrarla mai (es. la musica)", "Turn an app off to never record it (e.g. music)"},
    {"aud.apps.empty", "Nessuna app sta riproducendo audio in questo momento", "No app is playing audio right now"},
    // hotkeys
    {"hk.save", "Salva clip", "Save clip"},
    {"hk.record", "Avvia / ferma registrazione", "Start / stop recording"},
    {"hk.gallery", "Apri galleria", "Open gallery"},
    {"hk.press", "Premi la combinazione…", "Press the combination…"},
    {"hk.none", "Nessuna", "None"},
    {"hk.invalid", "Serve almeno un modificatore (Ctrl, Alt, Shift, Win)", "Needs a modifier (Ctrl, Alt, Shift, Win)"},
    // output
    {"out.folder", "Cartella delle clip", "Clips folder"},
    {"out.container", "Formato file", "File format"},
    {"out.container.hint", "MP4 va bene ovunque; MKV regge meglio i crash", "MP4 plays everywhere; MKV survives crashes better"},
    {"out.name", "Nome dei file", "File names"},
    {"out.name.hint", "Usa {app}, {date}, {time}", "Use {app}, {date}, {time}"},
    {"out.preview", "Esempio: {}", "Example: {}"},
    // general
    {"gen.autostart", "Avvia con Windows", "Start with Windows"},
    {"gen.sounds", "Suono al salvataggio", "Sound when saving"},
    {"gen.toast", "Posizione delle notifiche", "Notification position"},
    {"corner.tl", "In alto a sinistra", "Top left"},
    {"corner.tr", "In alto a destra", "Top right"},
    {"corner.bl", "In basso a sinistra", "Bottom left"},
    {"corner.br", "In basso a destra", "Bottom right"},
    {"gen.language", "Lingua", "Language"},
    {"gen.accent", "Colore principale", "Accent colour"},
    // footer
    {"btn.apply", "Applica", "Apply"},
    {"btn.revert", "Annulla modifiche", "Discard changes"},
    {"footer.unsaved", "Modifiche non applicate", "Unapplied changes"},
    {"footer.saved", "Impostazioni applicate", "Settings applied"},
    // gallery
    {"gal.search", "Cerca clip…", "Search clips…"},
    {"gal.empty", "Ancora nessuna clip. Premi {} mentre giochi per salvare gli ultimi secondi.",
     "No clips yet. Press {} while playing to save the last seconds."},
    {"gal.noresults", "Nessuna clip corrisponde alla ricerca", "No clip matches your search"},
    {"gal.today", "Oggi", "Today"},
    {"gal.yesterday", "Ieri", "Yesterday"},
    {"gal.count", "{} clip · {}", "{} clips · {}"},
    {"gal.folder", "Apri cartella", "Open folder"},
    {"gal.rename", "Rinomina", "Rename"},
    {"gal.delete", "Sposta nel Cestino", "Move to Recycle Bin"},
    {"gal.reveal", "Mostra nella cartella", "Show in folder"},
    {"gal.copy", "Copia file", "Copy file"},
    {"gal.copied", "File copiato negli appunti", "File copied to clipboard"},
    {"gal.deleted", "Clip spostata nel Cestino", "Clip moved to Recycle Bin"},
    {"gal.renamed", "Clip rinominata", "Clip renamed"},
    // player
    {"pl.back", "Galleria", "Gallery"},
    {"pl.tracks", "Tracce audio", "Audio tracks"},
    {"pl.tracks.hint", "Spegni o abbassa una traccia: lo senti subito", "Mute or lower a track: you hear it right away"},
    {"pl.export", "Esporta", "Export"},
    {"pl.export.hint", "Il video viene copiato senza perdita, l'audio mixato in una traccia",
     "Video is copied losslessly, audio is mixed into one track"},
    {"pl.separate", "Tieni anche le tracce scelte separate", "Also keep the chosen tracks separate"},
    {"pl.exporting", "Esportazione… {}%", "Exporting… {}%"},
    {"pl.exported", "Esportata: {}", "Exported: {}"},
    {"pl.error", "Impossibile aprire la clip", "Cannot open this clip"},
    {"pl.notracks", "Questa clip non ha audio", "This clip has no audio"},
};

std::atomic<int> g_lang{0};  // 0 = it, 1 = en

const std::unordered_map<std::string_view, const I18nEntry*>& index() {
  static const auto idx = [] {
    std::unordered_map<std::string_view, const I18nEntry*> m;
    for (const auto& e : kTable) m[e.key] = &e;
    return m;
  }();
  return idx;
}

}  // namespace

const std::vector<I18nEntry>& i18nTable() { return kTable; }

void setLanguage(std::string_view lang) { g_lang = lang == "en" ? 1 : 0; }

const char* tr(std::string_view key) {
  const auto& idx = index();
  const auto it = idx.find(key);
  if (it == idx.end()) {
    thread_local std::string fallback;
    fallback = key;
    return fallback.c_str();
  }
  return g_lang == 1 ? it->second->en : it->second->it;
}

}  // namespace clipper
