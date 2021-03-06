#include "nodes/piano.hpp"

namespace audionodes {

static NodeTypeRegistration<Piano> registration("PianoNode");

Piano::Piano() :
    Node({SocketType::midi, SocketType::audio}, SocketTypeList(4, SocketType::audio), {})
{
  existing_note.fill(nullptr);
}

Universe::Descriptor Piano::infer_polyphony_operation(std::vector<Universe::Pointer>) {
  Universe::Pointer mono(new Universe()), uni(new Universe(true, voices.size()));
  return Universe::Descriptor(mono, mono, uni);
}

void Piano::process(NodeInputWindow &input) {
  input.universes.output->ensure(voices.size());
  const MidiData &midi = input[InputSockets::midi_in].get<MidiData>();
  SigT sust = std::max(SigT(0), input[InputSockets::sustain_time][0][0]);
  new_voices.clear();
  for (const MidiData::Event event : midi.events) {
    unsigned char note = event.get_note();
    switch (event.get_type()) {
      case MidiData::EType::note_on:
        if (existing_note[note]) {
          existing_note[note]->released = true;
          if (sust == 0) existing_note[note]->dead = true;
        }
        new_voices.push_back({note, SigT(std::pow(2, (note-69)/12.)*440), event.get_velocity()/SigT(127)});
        existing_note[note] = &new_voices.back();
        break;
      case MidiData::EType::note_off:
        if (existing_note[note]) {
          existing_note[note]->released = true;
          if (sust == 0) existing_note[note]->dead = true;
        }
        break;
      case MidiData::EType::control:
        if (event.is_panic()) {
          for (auto &voice : voices) {
            voice.dead = true;
          }
          for (auto &voice : new_voices) {
            voice.dead = true;
          }
        }
        break;
      default:
        break;
    }
  }
  removed_tmp.clear();
  voices_tmp.clear();
  for (size_t i = 0; i < voices.size(); ++i) {
    if (voices[i].released && voices[i].since_rel > size_t(sust*RATE)) {
      voices[i].dead = true;
    }
    if (voices[i].dead) {
      removed_tmp.push_back(i);
    } else {
      voices_tmp.push_back(voices[i]);
    }
  }
  input.universes.output->update(removed_tmp, new_voices.size());
  voices.clear();
  size_t n = voices_tmp.size() + new_voices.size();
  voices.reserve(n);
  existing_note.fill(nullptr);
  for (VoiceState voice : voices_tmp) {
    voices.push_back(voice);
    existing_note[voice.note] = &voices.back();
  }
  for (VoiceState voice : new_voices) {
    voices.push_back(voice);
    existing_note[voice.note] = &voices.back();
  }
  AudioData::PolyWriter
    frequency(output_window[OutputSockets::frequency], n),
    velocity(output_window[OutputSockets::velocity], n),
    runtime(output_window[OutputSockets::runtime], n),
    decay(output_window[OutputSockets::decay], n);
  for (size_t i = 0; i < n; ++i) {
    VoiceState &voice = voices[i];
    frequency[i].fill(voice.freq);
    velocity[i].fill(voice.velocity);
    for (size_t j = 0; j < N; ++j) {
      runtime[i][j] = SigT(voice.age++)/RATE;
    }
    if (voice.released) {
      for (size_t j = 0; j < N; ++j) {
        decay[i][j] = std::max(SigT(0), (sust*RATE-SigT(voice.since_rel++))/(sust*RATE));
      }
    } else decay[i].fill(1);
  }
}

}
