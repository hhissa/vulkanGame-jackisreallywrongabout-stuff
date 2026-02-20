#pragma once
#include <array>
#include <string>
#include <vector>

struct QAPair {
  std::string prompt;
  std::array<std::string, 3> answers;
};

class QASession {
public:
  QASession() : m_currentIndex(0) {
    m_questions = {
        {"What is the capital of France?", {"Berlin", "Paris", "Madrid"}},
        {"How many sides does a triangle have?", {"Four", "Five", "Three"}},
        {"What is the largest planet in our solar system?",
         {"Saturn", "Jupiter", "Neptune"}},
        {"What is the chemical symbol for water?", {"CO2", "O2", "H2O"}},
        {"Who wrote Romeo and Juliet?",
         {"Charles Dickens", "William Shakespeare", "Jane Austen"}},

        // Add more questions here:
        // { "Your prompt?", { "Answer A", "Answer B", "Answer C" } },
    };
  }

  // Add a question with exactly 3 answers
  void addQuestion(const std::string &prompt,
                   const std::array<std::string, 3> &answers) {
    m_questions.push_back({prompt, answers});
  }

  // --- State queries ---

  const std::string &getCurrentPrompt() const {
    return m_questions[m_currentIndex].prompt;
  }

  const std::array<std::string, 3> &getCurrentAnswers() const {
    return m_questions[m_currentIndex].answers;
  }

  const std::string &getAnswer(int index) const {
    return m_questions[m_currentIndex].answers[index];
  }

  int getCurrentIndex() const { return m_currentIndex; }

  int getTotalQuestions() const { return static_cast<int>(m_questions.size()); }

  bool isFinished() const {
    return m_currentIndex >= static_cast<int>(m_questions.size());
  }

  // --- State transitions ---

  bool advance() {
    if (m_currentIndex < static_cast<int>(m_questions.size()) - 1) {
      ++m_currentIndex;
      return true;
    }
    return false; // already at last question
  }

  bool retreat() {
    if (m_currentIndex > 0) {
      --m_currentIndex;
      return true;
    }
    return false; // already at first
  }

  void reset() { m_currentIndex = 0; }

  void jumpTo(int index) {
    if (index >= 0 && index < static_cast<int>(m_questions.size())) {
      m_currentIndex = index;
    }
  }

private:
  std::vector<QAPair> m_questions;
  int m_currentIndex;
};
