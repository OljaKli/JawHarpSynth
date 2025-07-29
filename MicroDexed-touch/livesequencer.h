#ifndef LIVESEQUENCER_H
#define LIVESEQUENCER_H

#include <string>
#include <vector>
#include <list>
#include "MIDI.h"
#include "LCDMenuLib2_typedef.h"
#include <unordered_map>
#include <unordered_set>
#include <set>
#include "UI.h"

typedef void (*SetupFn)(void*);
class UI_LiveSequencer;

class LiveSequencer {

public:
  static constexpr uint8_t LIVESEQUENCER_TRACKS_PER_SCREEN = 6;
  static constexpr uint8_t LIVESEQUENCER_NUM_TRACKS = 2 * LIVESEQUENCER_TRACKS_PER_SCREEN;
  static constexpr uint8_t LIVESEQUENCER_NUM_LAYERS = 4;

  enum EventSource : uint8_t {
    EVENT_SONG = 0,
    EVENT_PATTERN = 1,
  };

  enum LayerMode {
    LAYER_MUTE = 0,
    LAYER_MERGE,
    LAYER_DELETE,
    LAYER_CLEAR_CC,
    LAYER_CLEAR_PB,
    LAYER_MODE_NUM
  };

  enum Device {
    DEVICE_INTERNAL = 0,
    DEVICE_MIDI_USB = 1,
    DEVICE_MIDI_DIN = 2,
    DEVICE_MIDI_INT = 3
  };

   // !!! never change existing numbers, used as IDs in saved performances !!!
  enum InternalInstrument {
    INSTR_DRUM = 0,
    INSTR_DX1 = 1,
    INSTR_DX2 = 2,
    INSTR_EP = 3,
    INSTR_MS1 = 4,
    INSTR_MS2 = 5,
    INSTR_BRD = 6,
    INSTR_MSP1 = 7,
    INSTR_MSP2 = 8,
    INSTR_SLC = 9,
    // The gap here is because in the other sequencer, all instruments are managed as the same thing, even the external 16+16+16=48 MIDI Channels (+ a safety gap)
    INSTR_DX3 = 70,  
    INSTR_DX4 = 71, 
    INSTR_MAX = 12
  };

  struct MidiEvent {
    EventSource source;
    uint16_t patternMs;
    uint8_t patternNumber;
    uint8_t track;
    uint8_t layer;
    midi::MidiType event;
    uint8_t note_in;          // For CC: controller number, for Pitch Bend: LSB
    uint8_t note_in_velocity; // For CC: value, for Pitch Bend: MSB
  };

  struct Track {
    midi::Channel channel;
    char name[10];
    uint8_t layerMutes;
    LCDML_FuncPtr_pu8 screen;
    SetupFn screenSetupFn;
    std::unordered_multiset<uint8_t> activeNotes[LIVESEQUENCER_NUM_LAYERS];
    std::pair<bool, uint8_t> pitchBend[LIVESEQUENCER_NUM_LAYERS];
  };

  struct TrackSettings {
    uint8_t device;
    uint8_t instrument;
    uint8_t layerCount;
    uint8_t quantizeDenom;
    uint8_t velocityLevel; // 0: original, 1 - 10: 10-100%
    uint8_t songStartLayerMutes;
  };

  /**
   * Automation types for Live Sequencer
   * General-purpose high CC number from 102..119 range should be safe.
   * WARNING: Breaking change with old performances that had
   * 0 and 1 as mute on/off.
   */
  enum AutomationType : uint8_t {
    TYPE_MUTE_ON = 102, 
    TYPE_MUTE_OFF = 103
  };

  struct FillNotes {
    uint8_t number;
    uint8_t offset;
    uint8_t velocityLevel;
  };

  enum ArpMode : uint8_t {
    ARP_UP = 0,
    ARP_DOWN,
    ARP_UPDOWN,
    ARP_UPDOWN_P,
    ARP_DOWNUP,
    ARP_DOWNUP_P,
    ARP_RANDOM,
    ARP_CHORD,
    ARP_MODENUM
  };

  struct ArpNote {
    uint16_t offDelay;
    uint8_t track;
    std::vector<uint8_t> notes;
  };

  struct ArpSettings {
    uint16_t delayToNextArpOnMs;
    uint8_t enabled;
    uint8_t amount; // 1, 2, ... per bar
    uint8_t octaves;
    uint8_t source;
    ArpMode mode;
    uint8_t loadPerBar;
    uint8_t noteRepeat;
    uint8_t velocityLevel;
    uint8_t notePlayCount;
    uint16_t length; // >100% pulse width possible
    int8_t swing;
    uint8_t latch; // keep notes or drop them
    uint8_t freerun; // do not restart arp on pattern start
    bool keysChanged;
    bool arpSettingsChanged;
    std::vector<uint8_t> arpNotesIn;
    std::vector<uint8_t> arpNotes;
    std::vector<uint8_t>::iterator arpIt;
  };

#ifdef PSRAM
  template<typename T>
  class Alloc {
  public:
    using value_type = T;

    Alloc() = default;

    template<typename U>
    constexpr Alloc(const Alloc<U>&) noexcept {}

    T* allocate(std::size_t n) {
        DBG_LOG(printf("alloc %i!\n", n));
        return static_cast<T*>(extmem_malloc(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) noexcept {
        extmem_free((uint8_t*)p);
    }

    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new(p) U(std::forward<Args>(args)...);
    }

    template<typename U>
    void destroy(U* p) noexcept {
        p->~U();
    }

    friend bool operator==(const Alloc&, const Alloc&) { return true; }
    friend bool operator!=(const Alloc&, const Alloc&) { return false; }
  };

  typedef std::vector<MidiEvent, Alloc<MidiEvent>> EventVector;
#else
  typedef std::vector<MidiEvent> EventVector;
#endif

  struct LiveSeqData {
    // non - volatile
    TrackSettings trackSettings[LIVESEQUENCER_NUM_TRACKS];

    EventVector eventsList;
    std::unordered_map<uint8_t, EventVector> songEvents;
    uint8_t numberOfBars = 4;

    // volatile
    bool processMidiIn;
    std::string performanceName;
    std::unordered_set<uint8_t> instrumentChannels;
    Track tracks[LIVESEQUENCER_NUM_TRACKS];
    ArpSettings arpSettings;
    uint8_t lastSongEventPattern; // because using unordered map above we need to know last index to be able to know song length (eg. for song loop)
    uint8_t currentPattern = 0;
    FillNotes fillNotes = { 4, 0 }; // user default
    unsigned long patternLengthMs;
    uint8_t activeTrack = 0;
    elapsedMillis patternTimer;
    std::unordered_map<uint8_t, MidiEvent> notesOn;
    EventVector pendingEvents;
    uint8_t songPatternCount = 0;
    uint8_t songLayerCount = 0;
    uint8_t lastPlayedNote = 0;
    uint8_t remainingCountIns = 0;

    bool isStarting = false;
    bool isRunning = false;
    bool isRecording = false;

    bool isSongMode = false;
    bool recordedToSong = false;
    int currentBpm = 90;
    uint8_t performanceID = 0;
    uint8_t songMuteQuantizeDenom = 1;
  };

  LiveSequencer();
  void initOnce(void);
  LiveSequencer::LiveSeqData* getData(void);
  void songLayerAction(uint8_t layer, LayerMode action);
  bool trackLayerAction(uint8_t track, uint8_t layer, LayerMode action, bool &clearLayer);
  void handleMidiEvent(uint8_t inChannel, midi::MidiType event, uint8_t note, uint8_t velocity);
  void handlePatternBegin(void);
  void start(void);
  void stop(void);
  void onStarted(void);
  void onStopped(void);
  void init(void);
  void onGuiInit(void);
  void onArpSourceChanged(void);
  void setLayerMuted(uint8_t track, uint8_t layer, bool isMuted, bool recordToSong = false);
  void changeNumberOfBars(uint8_t num);
  void deleteAllSongEvents(void);
  void fillTrackLayer();
  void deleteLiveSequencerData(void);
  void setArpEnabled(bool enabled);
  uint32_t timeToMs(uint8_t patternNumber, uint16_t patternMs) const;
  void startCountIn(void);
  void getDeviceName(uint8_t device, char* name, char* sub) const;
  void getInstrumentName(uint8_t device, uint8_t instrument, char* name, char* sub) const;
  void changeTrackInstrument(uint8_t track, uint8_t newDevice, uint8_t newInstrument);
  void loadOldTrackInstruments(void); // load track instruments from normal sequencer
  void requestSortEvents(void);
  void setActiveTrack(uint8_t track);

  struct NotePair {
    MidiEvent& noteOn;
    MidiEvent& noteOff;
    bool isMuted;
  };

  void addNotePair(MidiEvent noteOn, MidiEvent noteOff);
  std::vector<NotePair> getNotePairsFromTrack(uint8_t track, uint8_t& lowestNote, uint8_t& highestNote, uint8_t patternFrom = 0, uint8_t patternTo = 0xFF);

  static void printNotePairs(std::vector<NotePair> notePairs);
  static void printEvent(int i, MidiEvent e);
  static const std::string getEventName(midi::MidiType event);
  static const std::string getEventSource(EventSource source);
  void checkBpmChanged(void);

private:
  LiveSeqData data;
  std::set<uint8_t> activeArpKeys;
  std::set<uint8_t> pressedKeys;
  std::vector<ArpNote> activeArps;
  EventVector::iterator playIterator;
  bool sortRequested = false;

  void checkLoadNewArpNotes(void);
  void onSongStopped(void);
  void updateTrackChannels(bool initial = false);
  void updateInstrumentChannels(void);
  void addPendingNotes(bool incrementLayer = true);
  void refreshSongLength(void);
  void applySongStartLayerMutes(void);
  void printEvents();
  void loadNextEvent(int timeMs);
  void allTrackNotesOff(const uint8_t track);
  void allLayerNotesOff(const uint8_t track, const uint8_t layer);
  void allNotesOff(void);
  void resetTrackControllers(uint8_t track);
  void resetControllers();
  void playNextEvent(void);
  void playNextArpNote(void);
  void playArp(const midi::MidiType type, const ArpNote arp);
  bool timeQuantization(MidiEvent& e, uint8_t denom);
  void checkAddMetronome(void);
  void performLayerAction(LayerMode action, MidiEvent& e, uint8_t layer);
  void sortEvents(void);

  static bool sortMidiEvent(MidiEvent& a, MidiEvent& b) {
    // + a.source is a hack to sort song events before pattern events if the have same time
    return ((a.patternNumber * 5000) + a.patternMs + a.source) < ((b.patternNumber * 5000) + b.patternMs + b.source); // FIXME: patternLengthMs
  }
};

void UI_func_livesequencer(uint8_t param);
void handle_touchscreen_live_sequencer(void);

#endif // LIVESEQUENCER_H
