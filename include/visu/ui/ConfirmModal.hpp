#pragma once
#include "visu/ui/Modal.hpp"

#include <functional>
#include <string>
#include <vector>

struct ModalChoice {
  std::string label;
  std::function<void()> onClick;
};

class ConfirmModal : public Modal {
private:
  std::string m_title;
  std::string m_text;
  std::vector<ModalChoice> m_choices;

public:
  ConfirmModal(std::string _title, std::string _text,
               std::vector<ModalChoice> _choices);
  const char *Id() const override { return m_title.c_str(); }
  bool Draw() override;
};
