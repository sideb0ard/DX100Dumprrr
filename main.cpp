#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// hefty amount of code "borrowed" from
// https://github.com/rogerallen/dxsyx

const int SYX_FILE_SIZE = 4096 + 8;
const int SYX_VOICE_SIZE = 4096 / 32;
const int SYX_NUM_VOICES = 32;
const int SYX_NUM_OSC = 4;

struct Operator {
  int attack_rate{0};
  int decay1_rate{0};
  int decay2_rate{0};
  int release_rate{0};
  int decay1_level{0};
  int level_scaling{0};
  int amp_mod_en{0};                // same byte
  int eg_bias_sensitivity{0};       // same byte
  int key_velocity_sensitivity{0};  // same byte
  int out{0};
  int frequency_ratio{0};
  int rate_scaling{0};  // same byte
  int detune{0};        // same byte
};

struct DXVoice {
  Operator op1;
  Operator op2;
  Operator op3;
  Operator op4;

  int lfo_sync{0};   // same b
  int feedback{0};   // same b
  int algorithm{0};  // same b
  int lfo_speed{0};
  int lfo_delay{0};
  int pitch_mod_depth{0};
  int amp_mod_depth{0};

  // same byte
  int pitch_mod_sensitivity{0}, amp_mod_sensitivity{0}, lfo_wave{0};

  int transpose{0};
  int pitch_bend_range{0};

  // same byte
  int chorus{0}, mono{0}, sustain{0}, portamento_en{0}, portamento_mode{0};

  int portamento_time{0};
  int footcontrol_volume{0};
  int mod_wheel_pitch{0};
  int mod_wheel_ampli{0};
  int breath_control_pitch{0};
  int breath_control_ampli{0};
  int breath_control_p_bias{0};
  int breath_control_e_bias{0};
  std::string name{0};

  // unused in dx100
  int pr1{0};
  int pr2{0};
  int pr3{0};
  int pl1{0};
  int pl2{0};
  int pl3{0};
};

namespace {

std::vector<uint8_t> filedata;
uint32_t file_read_idx{0};
std::vector<DXVoice> voices;

static const float dx_ratios[] = {
    0.5,   0.71,  0.78,  0.87,  1.0,   1.41,  1.57,  1.73,  2.,    2.82,  3.,
    3.14,  3.46,  4.,    4.24,  4.71,  5.,    5.19,  5.65,  6.0,   6.28,  6.92,
    7.,    7.07,  7.85,  8.0,   8.48,  8.65,  9.0,   9.42,  9.89,  10.,   10.38,
    10.99, 11.0,  11.30, 12.0,  12.11, 12.56, 12.72, 13.0,  13.84, 14.0,  14.1,
    14.13, 15.,   15.55, 15.37, 15.70, 16.96, 17.27, 17.30, 18.37, 18.84, 19.03,
    19.78, 20.41, 20.76, 21.20, 21.98, 22.49, 23.53, 24.22, 25.95};
static const int NUM_RATIOS = 64;

bool ReadFile(const std::string& filename) {
  std::ifstream fl(filename, std::ios::binary);
  if (!fl.good()) {
    std::cerr << "NAE GOOD!\n";
    return false;
  }
  fl.seekg(0, std::ios::end);
  std::streamoff len = fl.tellg();
  if (len < SYX_FILE_SIZE) {
    throw std::runtime_error(std::string("filesize less than 4096+8 bytes."));
  } else if (len > SYX_FILE_SIZE) {
    std::cerr << "WARNING: filesize exceeds maximum expected size."
              << std::endl;
  }
  fl.seekg(0, std::ios::beg);
  filedata.resize(static_cast<size_t>(len));
  fl.read((char*)&filedata.front(), len);
  fl.close();

  return true;
}

char FixChar(char c) {
  if ((c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122)) {
    return std::tolower(c);
  }
  return '_';
}

uint8_t GetData() { return filedata[file_read_idx++]; }

void CheckHeader() {
  uint8_t syx_status = GetData();
  uint8_t syx_id = GetData();
  uint8_t syx_sub_status_channel_num = GetData();
  uint8_t syx_format_number = GetData();
  uint8_t syx_byte_count_ms = GetData();
  uint8_t syx_byte_count_ls = GetData();

  if (syx_status != 0xf0) {
    throw std::runtime_error(
        std::string("header status != 0xf0."));  // START OF MIDI SYSEX
  } else if (syx_id != 0x43) {
    throw std::runtime_error(std::string("header id != 0x43."));  // YAMAHA
  } else if (syx_format_number != 0x04) {
    throw std::runtime_error(
        std::string("header format != 0x04."));  // DX100 - 04 means 32 voice,
                                                 // 03 means 1 voice
  } else if ((syx_byte_count_ms != 0x20) && (syx_byte_count_ls != 0x00)) {
    throw std::runtime_error(std::string("header byte_count not 4k."));
  }
}

Operator ExtractOperatorValues() {
  Operator op;

  op.attack_rate = GetData();
  op.decay1_rate = GetData();
  op.decay2_rate = GetData();
  op.release_rate = GetData();
  op.decay1_level = GetData();

  op.level_scaling = GetData();

  int ame_ebs_kvs = GetData();
  op.amp_mod_en = (ame_ebs_kvs & 0x40) >> 6;
  op.eg_bias_sensitivity = (ame_ebs_kvs & 0x38) >> 3;
  op.key_velocity_sensitivity = (ame_ebs_kvs & 0x07);

  op.out = GetData();
  op.frequency_ratio = GetData();

  int rs_dbt = GetData();
  op.rate_scaling = (rs_dbt & 0x18) >> 3;
  op.detune = (rs_dbt & 0x07);

  return op;
}

DXVoice ExtractDXVoiceValues() {
  DXVoice voice;
  voice.op1 = ExtractOperatorValues();
  voice.op2 = ExtractOperatorValues();
  voice.op3 = ExtractOperatorValues();
  voice.op4 = ExtractOperatorValues();

  int sy_fbl_algorithm = GetData();
  voice.lfo_sync = (sy_fbl_algorithm & 0x40) >> 6;
  voice.feedback = (sy_fbl_algorithm & 0x38) >> 3;
  voice.algorithm = sy_fbl_algorithm & 0x07;

  voice.lfo_speed = GetData();
  voice.lfo_delay = GetData();
  voice.pitch_mod_depth = GetData();
  voice.amp_mod_depth = GetData();

  int pms_ams_lfw = GetData();
  voice.pitch_mod_sensitivity = (pms_ams_lfw & 0xE0) >> 4;
  voice.amp_mod_sensitivity = (pms_ams_lfw & 0x18) >> 2;
  voice.lfo_wave = (pms_ams_lfw & 0x03);

  voice.transpose = GetData();
  voice.pitch_bend_range = GetData();
  int ch_mo_su_po_pm = GetData();
  voice.chorus = (ch_mo_su_po_pm & 0x20) >> 4;
  voice.mono = (ch_mo_su_po_pm & 0x08) >> 3;
  voice.sustain = (ch_mo_su_po_pm & 0x04) >> 2;
  voice.portamento_en = (ch_mo_su_po_pm & 0x02) >> 1;
  voice.portamento_mode = (ch_mo_su_po_pm & 0x01);

  voice.portamento_time = GetData();
  voice.footcontrol_volume = GetData();
  voice.mod_wheel_pitch = GetData();
  voice.mod_wheel_ampli = GetData();
  voice.breath_control_pitch = GetData();
  voice.breath_control_ampli = GetData();
  voice.breath_control_p_bias = GetData();
  voice.breath_control_e_bias = GetData();
  std::array<char, 10> voice_name{0};
  for (int i = 0; i < 10; i++) {
    auto symb = FixChar(GetData());
    if (!isprint(symb)) {
      symb = '_';
    }
    voice_name[i] = symb;
  }
  voice.name = std::string(voice_name.begin(), voice_name.end());

  voice.pr1 = GetData();
  voice.pr2 = GetData();
  voice.pr3 = GetData();
  voice.pl1 = GetData();
  voice.pl2 = GetData();
  voice.pl3 = GetData();

  return voice;
}

double Scaley(double val, double old_min, double old_max, double new_min,
              double new_max) {
  if (old_max == old_min) {
    std::cerr << "Nah mate, cannae divide by zero.";
    return 0;
  }
  return new_min + (val - old_min) * (new_max - new_min) / (old_max - old_min);
}

std::string DumpSBOp(Operator op, int num) {
  std::stringstream ss;
  ss << "::m_op" << num << "_output_lvl=" << op.out << "::m_op" << num
     << "_ratio=" << dx_ratios[op.frequency_ratio] << "::m_op" << num
     << "_detune_cents=" << Scaley(op.detune, 0, 6, -2.6, 2.6) << "::m_eg"
     << num << "_attack_ms=" << Scaley(31 - op.attack_rate, 0, 31, 0, 1000)
     << "::m_eg" << num
     << "_decay_ms=" << Scaley(31 - op.decay1_rate, 0, 31, 0, 1000) << "::m_eg"
     << num << "_release_ms=" << Scaley(15 - op.release_rate, 0, 15, 0, 1000)
     << "::m_eg" << num
     << "_sustain_lvl=" << Scaley(15 - op.decay1_level, 0, 15, 0, 1);
  return ss.str();
}
void DumpSBVoice(DXVoice voice) {
  std::stringstream ss;
  ss << "::name=" << voice.name << "::m_voice_mode=" << voice.algorithm
     << "::m_op4_feedback=" << Scaley(voice.feedback, 0, 7, 0, 99)
     << DumpSBOp(voice.op1, 1) << DumpSBOp(voice.op2, 2)
     << DumpSBOp(voice.op3, 3) << DumpSBOp(voice.op4, 4);

  std::cout << ss.str() << std::endl;
  // std::cout << "LFOSync:" << voice.lfo_sync << " lfo_speed:" <<
  // voice.lfo_speed
  //           << " lfo_delay:" << voice.lfo_delay
  //           << " lfo_wave:" << voice.lfo_wave << std::endl;
  // std::cout << "transpose:" << voice.transpose << " chorus:" << voice.chorus
  //           << " sustain:" << voice.sustain
  //           << " porta_en:" << voice.portamento_en
  //           << " porta_mode:" << voice.portamento_mode
  //           << " porta:" << voice.portamento_time << std::endl;
}

void DumpOp(Operator op) {
  std::cout << "attack_rate:" << op.attack_rate
            << " decay1_rate:" << op.decay1_rate
            << " decay2_rate:" << op.decay2_rate
            << " release_rate:" << op.release_rate
            << " decay1_level:" << op.decay1_level << std::endl;

  std::cout << "level_scaling:" << op.level_scaling
            << " amp_mod_en:" << op.amp_mod_en
            << " eg_bias_sensitivity:" << op.eg_bias_sensitivity
            << " key_velocity_sensitivity:" << op.key_velocity_sensitivity
            << std::endl;
  std::cout << "out:" << op.out << " ratio:" << op.frequency_ratio
            << " rate_scaling:" << op.rate_scaling << " detune:" << op.detune
            << std::endl;
}

void DumpVoice(DXVoice voice) {
  std::cout << "\nDX Voice: " << voice.name << " Algo:" << voice.algorithm
            << " fb:" << voice.feedback
            << " pitchbend_range:" << voice.pitch_bend_range << std::endl;
  std::cout << "::OP1::\n";
  DumpOp(voice.op1);
  std::cout << "::OP2::\n";
  DumpOp(voice.op2);
  std::cout << "::OP3::\n";
  DumpOp(voice.op3);
  std::cout << "::OP4::\n";
  DumpOp(voice.op4);
  std::cout << "::GLOBALS::\n";
  std::cout << "LFOSync:" << voice.lfo_sync << " lfo_speed:" << voice.lfo_speed
            << " lfo_delay:" << voice.lfo_delay
            << " lfo_wave:" << voice.lfo_wave << std::endl;
  std::cout << "transpose:" << voice.transpose << " chorus:" << voice.chorus
            << " sustain:" << voice.sustain
            << " porta_en:" << voice.portamento_en
            << " porta_mode:" << voice.portamento_mode
            << " porta:" << voice.portamento_time << std::endl;
}

void UnpackVoices() {
  for (int i = 0; i < SYX_NUM_VOICES; i++) {
    int next_voice = file_read_idx + SYX_VOICE_SIZE;
    auto dxvoice = ExtractDXVoiceValues();
    voices.push_back(dxvoice);
    file_read_idx = next_voice;
  }
}

void CheckFooter() {
  uint8_t syx_checksum = GetData();
  uint8_t syx_eox = GetData();
  if (syx_eox != 0xf7) {
    throw std::runtime_error(std::string("EOX != 0xf7."));  // END OF MIDI SYSEX
  }
}

}  // namespace

const char* sb_dump_keyword{"dump_sb"};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " input_file.syx <dump_sb>"
              << std::endl;
    return 0;
  }
  if (ReadFile(argv[1])) {
    CheckHeader();
    UnpackVoices();
    CheckFooter();
    if (argc == 3 && strcmp(sb_dump_keyword, argv[2]) == 0) {
      for (int i = 0; i < SYX_NUM_VOICES; i++) {
        DumpSBVoice(voices[i]);
      }
    } else {
      for (int i = 0; i < SYX_NUM_VOICES; i++) {
        DumpVoice(voices[i]);
      }
    }
  }

  return 0;
}
