#include <string>
#include <algorithm>
#include "exception.hh"
#include "webm_info.hh"

using namespace std;

template<typename T> T BinaryReader::read(bool switch_endian)
{
  string data = fd_.read(sizeof(T));
  return read_raw<T>(data, sizeof(T), switch_endian);
}

uint64_t BinaryReader::size()
{
  uint64_t current_pos = pos();
  uint64_t fsize = seek(0, SEEK_END);
  seek(current_pos, SEEK_SET);
  return fsize;
}

WebmParser::WebmParser(const string & filename)
  : br_(filename), elements_()
{
  parse(br_.size());
}

void WebmParser::parse(uint64_t max_pos)
{
  while (br_.pos() < max_pos ) {
    const uint32_t tag = scan_tag();
    if (tag == 0) {
      return; /* terminate */
    }
    uint64_t data_size = scan_data_size();
    if (data_size == static_cast<uint64_t>(-1)) {
      auto elem = make_shared<WebmElement>(tag, "");
      elements_.emplace_back(elem);
      continue;
    }
    if (find(begin(master_elements), end(master_elements), tag)
        == end(master_elements)) {
      auto elem = make_shared<WebmElement>(tag, br_.read_bytes(data_size));
      elements_.emplace_back(elem);
    } else {
      /* master block */
      auto elem = make_shared<WebmElement>(tag, "");
      elements_.emplace_back(elem);
      parse(br_.pos() + data_size);
    }
  }
}

uint64_t WebmParser::scan_tag()
{
  const uint8_t first_byte = br_.read_uint8();
  const uint8_t mask = 0xFF;
  uint32_t tag_size = 4; /* by default it's an impossible state */
  if (!first_byte) {
    return 0; /* end of stream */
  }
  for (uint32_t i = 0; i < 4; i++) {
    if (first_byte & (0x80 >> i)) {
      tag_size = i;
      break;
    }
  }
  if (tag_size == 4) {
    cout << unsigned(first_byte) << endl;
    throw runtime_error("Invalid tag type at pos " + to_string(br_.pos()));
  }
  return decode_bytes(tag_size, first_byte, mask);
}

uint64_t WebmParser::decode_bytes(uint32_t tag_size, uint8_t first,
                                uint8_t first_mask)
{
  uint64_t value = first & first_mask;
  for (uint32_t i = 0; i < tag_size; i++) {
    uint8_t next_byte = br_.read_uint8();
    value = (value << 8) + next_byte;
  }
  return value;
}

uint64_t WebmParser::scan_data_size()
{
  uint32_t size = 0;
  uint32_t mask = 0xFF;

  uint8_t first_byte = br_.read_uint8();
  for (int i = 0; i < 8; i++) {
    if (first_byte & (0x80 >> i)) {
      size = i;
      mask = 0x7F >> i;
      break;
    }
  }
  return decode_bytes(size, first_byte, mask);
}

void WebmParser::print()
{
  for (const auto & elem : elements_) {
    elem->print();
  }
}

shared_ptr<WebmElement> WebmParser::find_first_elem(uint32_t tag)
{
  for (auto & elem : elements_) {
    if (elem->tag() == tag) {
      return elem;
    }
  }
  return nullptr;
}

vector<shared_ptr<WebmElement>> WebmParser::find_all_elem(uint32_t tag)
{
  vector<shared_ptr<WebmElement>> result;
  for (auto & elem : elements_) {
    if (elem->tag() == tag) {
      result.emplace_back(elem);
    }
  }
  return result;
}

uint32_t WebmInfo::get_timescale()
{
  auto elm = parser_.find_first_elem(0x002ad7b1);
  if (elm) {
    uint32_t data_size = elm->size();
    string data = elm->value();
    uint32_t timescale = read_raw<uint32_t>(data, data_size);
    /* because it's timescode scale, we need to transform it to
     * mp4's timescale */
    return 1000000000 / timescale;
  } else {
    return 0;
  }
}

uint32_t WebmInfo::get_bitrate()
{
  uint32_t total_size = 0;
  auto elems = parser_.find_all_elem(0x000000a3);
  for (const auto & elem : elems) {
    /* ignore the header size, which is much smaller than the actual size */
    total_size += elem->size();
  }
  return total_size;
}

uint32_t WebmInfo::get_duration()
{
  /* get duration from cue duration */
  auto elm = parser_.find_first_elem(0x000000b2);
  if (elm) {
    uint32_t data_size = elm->size();
    string data = elm->value();
    uint32_t result = read_raw<uint32_t>(data, data_size);
    return result;
  } else {
    return 0;
  }
}

uint32_t WebmInfo::get_sample_rate()
{
  auto elm = parser_.find_first_elem(0x000000b5);
  if (elm) {
    uint32_t data_size = elm->size();
    string data = elm->value();
    if (data_size == 4) {
      float value = read_raw<float>(data, data_size);
      return static_cast<uint32_t>(value);
    } else if (data_size == 8) {
      double value = read_raw<double>(data, data_size);
      return static_cast<uint32_t>(value);
    } else {
      throw runtime_error("Invalid sampling frequency");
    }
  } else {
    return 0;
  }
}
