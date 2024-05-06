#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double EPSILON = 1e-6;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    Document() = default;

    Document(int id, double relevance, int rating)
        : id(id)
        , relevance(relevance)
        , rating(rating) {
    }

    int id = 0;
    double relevance = 0.0;
    int rating = 0;
};

template <typename StringContainer>
set<string> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    set<string> non_empty_strings;
    for (const string& str : strings) {
        if (!str.empty()) {
            non_empty_strings.insert(str);
        }
    }
    return non_empty_strings;
}

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    SearchServer() = default;
    
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {
        for(const string& word : stop_words){
            if(!IsValidWord(word)) throw invalid_argument("Bad constructors stop arguments"s);
        }
    }

    explicit SearchServer(const string& stop_words_text)
        : SearchServer(
            SplitIntoWords(stop_words_text))  // Invoke delegating constructor from string container
    {
    }

    int GetDocumentId(int index) const {
        if(index >= 0 && index < GetDocumentCount()){
            return ids_.at(index);
        }
        else {
            throw out_of_range("Wrong index of document"s);
        }
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status,
                                   const vector<int>& ratings) {
        if(documents_.count(document_id)) {
            throw invalid_argument("Document ID is already exist"s);
        }
        if(document_id < 0) {
            throw invalid_argument("Document ID is negative"s);
        }

        const vector<string> words = SplitIntoWordsNoStop(document);

        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
        ids_.push_back(document_id);
    }

    template <typename DocumentPredicate>
    vector<Document> FindTopDocuments(const string& raw_query, DocumentPredicate document_predicate) const {
        if(!IsValidWord(raw_query)) {
            throw invalid_argument("Invalid requests text"s);
        }
        if(raw_query.empty()) {
            throw invalid_argument("Raw query is empty"s);
        }

        const Query query = ParseQuery(raw_query);
        
        if(query.plus_words.empty() && query.minus_words.empty() && !query.stop_words.empty()) {
            vector<Document> empty_str;
            return empty_str;
        }

        vector<Document> result = FindAllDocuments(query, document_predicate);
        sort(result.begin(), result.end(),
             [](const Document& lhs, const Document& rhs) {
                 if (abs(lhs.relevance - rhs.relevance) < EPSILON) {
                     return lhs.rating > rhs.rating;
                 } else {
                     return lhs.relevance > rhs.relevance;
                 }
             });
        if (result.size() > MAX_RESULT_DOCUMENT_COUNT) {
            result.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return result;
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(
            raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
                return document_status == status;
            });
    }
    
    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const {
        return documents_.size();
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        if(documents_.count(document_id) == 0) {
            throw invalid_argument("Wrong document ID (it has no exist)"s);
        }

        const Query query = ParseQuery(raw_query);
        if(query.plus_words.empty() && query.minus_words.empty() && !query.stop_words.empty()) {
            vector<string> empty_words;
            return make_tuple(empty_words, DocumentStatus::ACTUAL);
        }

        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return make_tuple(matched_words, documents_.at(document_id).status);
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };
    const set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;
    vector<int> ids_;

    static bool IsValidWord(const string& word) {
        // A valid word must not contain special characters
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
        });
    }

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if(!IsValidWord(word)){
                throw invalid_argument("Invalid word try to add"s);
            }
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
            throw invalid_argument("Invalid word try to request"s);
        }
        return {text, is_minus, IsStopWord(text)};
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
        set<string> stop_words;
    };

   Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if(query_word.data.empty()) {
                return {};
            }
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                } else {
                    query.plus_words.insert(query_word.data);
                }
            }
            else query.stop_words.insert(word);
        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template <typename DocumentPredicate>
    vector<Document> FindAllDocuments(const Query& query,
                                      DocumentPredicate document_predicate) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto &[document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id, document_data.status, document_data.rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto &[document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> result;
        for (const auto &[document_id, relevance] : document_to_relevance) {
            result.push_back(
                {document_id, relevance, documents_.at(document_id).rating});
        }
        return result;
    }
};

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
                     const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
                const string& hint) {
    if (!value) {
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "Assert("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

template <typename TestFunc>
void RunTestImpl(const TestFunc& func, const string& test_name) {
    func();
    cerr << test_name << " OK"s << endl;
}

#define RUN_TEST(func) RunTestImpl(func, #func)
#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))
#define ASSERT(a) AssertImpl((a), #a, __FILE__, __FUNCTION__, __LINE__, ""s);
#define ASSERT_HINT(a, hint) AssertImpl((a), #a, __FILE__, __FUNCTION__, __LINE__, (hint));

const double EPSILON_TEST = 1e-3;

// -------- Начало модульных тестов поисковой системы ----------
void TestConstructors(){
    {
        //SearchServer server("\x10"s);
    }
    {
        //vector<string> words = {"in"s, "\x10"s};
        //SearchServer server(words);
    }
}

void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server(""s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL_HINT(found_docs.size(), 1u, "Wrong find function"s);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL_HINT(doc0.id, doc_id, "Wrong ID by FindTopDocuments"s);
    }

    {
        SearchServer server("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_HINT(found_docs.empty(), "Stop words must be excluded from documents"s);
        
        const auto [matched_doc_42g, status42g] = server.MatchDocument("cat"s, 42);
        ASSERT_HINT(!matched_doc_42g.empty(), "Wrong to match good words"s);
        
        const auto [matched_doc_42b, status42b] = server.MatchDocument("-cat"s, 42);
        ASSERT_HINT(matched_doc_42b.empty(), "Wrong to match minus words"s);
        
        const auto [matched_doc_stop, status_stop] = server.MatchDocument("in"s, 42);
        ASSERT_HINT(matched_doc_stop.empty(), "Wrong to match stop words"s);

    }
    {
        //SearchServer server(""s);
        //server.AddDocument(2, "чук \x02 гек", DocumentStatus::ACTUAL, {3});
    }
    {   
        /* SearchServer server(""s);
        server.AddDocument(1, "чук и гек", DocumentStatus::ACTUAL, {3});
        server.AddDocument(1, "пьют чай", DocumentStatus::ACTUAL, {3, 4, 1}); */
    }
    {
        //SearchServer server(""s);
        //server.AddDocument(-11, "чук и гек", DocumentStatus::ACTUAL, {3});
    }
}

void TestMinusWordsExcluded(){
    SearchServer server(""s);
    server.AddDocument(12,"sweet home alabama in"s, DocumentStatus::ACTUAL, {1});
    server.AddDocument(4,"love me tender love me too"s, DocumentStatus::ACTUAL, {12, 1, 5});
    {
        const auto found_docs = server.FindTopDocuments("-in love"s);
        ASSERT_EQUAL_HINT(found_docs.size(), 1u, "Minus words should be deleted"s);
        ASSERT(found_docs[0].id == 4);
    }
    {
        const auto found_docs = server.FindTopDocuments("in -love"s);
        ASSERT_EQUAL_HINT(found_docs.size(), 1u, "Minus words should be deleted"s);
        ASSERT(found_docs[0].id == 12);
    }
    {
        const auto found_docs = server.FindTopDocuments("-in -love"s);
        ASSERT_EQUAL_HINT(found_docs.size(), 0, "Minus words should be deleted"s);
    }
}

void TestMatchFunction() {
    SearchServer server(""s);
    const string expected_word = "love"s;

    server.AddDocument(12,"sweet home alabama in"s, DocumentStatus::ACTUAL, {1});
    server.AddDocument(4,"love me tender love me too"s, DocumentStatus::ACTUAL, {12, 1, 5});
    server.AddDocument(42,"I sit and wait any angels"s, DocumentStatus::BANNED, {-2, 3});
    {
        const auto [docs, status] = server.MatchDocument("love sweet"s, 4);
        ASSERT_HINT((docs.size() == 1u) && (docs.at(0) == expected_word), "It has wrong plus word"s);
        ASSERT_EQUAL_HINT(static_cast<int> (status), static_cast<int> (DocumentStatus::ACTUAL), "It has wrong Document status"s);
    }
    {
        const auto [docs, status] = server.MatchDocument("love -love"s, 4);
        ASSERT_HINT(docs.size()== 0u, "It has wrong minus words"s);
    }
    {
        const auto [docs, status] = server.MatchDocument("sweet -home"s, 12);
        ASSERT_HINT(docs.size()== 0u, "It has wrong minus words"s);
    }
    {
        const auto [docs, status] = server.MatchDocument("sit -home"s, 42);
        ASSERT_HINT(docs.size()== 1u, "It has wrong minus words"s);
        ASSERT_EQUAL_HINT(static_cast<int> (status), static_cast<int> (DocumentStatus::BANNED), "It has wrong Document status"s);
    }
    {
        const auto [docs, status] = server.MatchDocument("sit any"s, 42);
        ASSERT_HINT(docs.size()== 2u, "It has added wrong plus words"s);
    }
    {
        const auto [docs, status] = server.MatchDocument("-sit -home"s, 42);
        ASSERT_HINT(docs.size()== 0u, "It has explicated wrong minus words"s);
    }
    {
        /*const auto [matched_doc_wrong, status_wrong] = server.MatchDocument("кот \x02", 2);
        ASSERT_HINT(!matched_doc_wrong.empty(), "Wrong to match non-usable words"s);
        
        const auto [matched_minmin, status_minmin] = server.MatchDocument("--кот", 2);
        ASSERT_HINT(!matched_minmin.empty(), "Wrong match --"s); 
        
        const auto [matched_id, status_id] = server.MatchDocument("кот", 2);
        ASSERT_HINT(!matched_id.empty(), "Wrong id"s); 

        const auto [matched_min, status_min] = server.MatchDocument("sit -", 42);
        ASSERT_HINT(!matched_min.empty(), "Wrong match -"s); */
    }
}

void TestFindTopDocumentsPredicate() {
    SearchServer server("и в на"s);
    server.AddDocument(12, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, {1});
    server.AddDocument(4, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {12, 1, 5});
    server.AddDocument(42, "ухоженный пёс выразительные глаза"s, DocumentStatus::BANNED, {-2, 5, 3});
   
    vector<Document> b = {{4, 0.6507, 6}, {42, 0.2746, 2}, {12, 0.1014, 1}}; 
    vector<Document> a = server.FindTopDocuments("пушистый ухоженный кот"s, 
                        [](int document_id, DocumentStatus status, int rating) {return document_id > 0;});
    {
        ASSERT_HINT(a.size() == 3u, "Wrong found amount of documents"s);
        ASSERT_HINT(abs(a[0].relevance - b[0].relevance) <= EPSILON_TEST, "Wrong relevance by EPSILON_TEST"s);
        ASSERT_HINT(abs(a[1].relevance - b[1].relevance) <= EPSILON_TEST, "Wrong relevance by EPSILON_TEST"s);
        ASSERT_HINT(abs(a[2].relevance - b[2].relevance) <= EPSILON_TEST, "Wrong relevance by EPSILON_TEST"s);
        ASSERT_EQUAL_HINT(a[0].rating, b[0].rating, "Wrong rating find"s);
        ASSERT_EQUAL_HINT(a[1].rating, b[1].rating, "Wrong rating find"s);
        ASSERT_EQUAL_HINT(a[2].rating, b[2].rating, "Wrong rating find"s);
    }
    {
        b = {{4, 0.6507, 6}};
        a = server.FindTopDocuments("пушистый ухоженный кот"s, 
            [](int document_id, DocumentStatus status, int rating) {return rating >= 5;});
        
        ASSERT_HINT(a.size() == 1u, "Wrong filtration by rating"s);
        ASSERT_HINT(abs(a[0].relevance - b[0].relevance) <= EPSILON_TEST, "Wrong relevance by EPSILON_TEST (with rating filter)"s);
        ASSERT_EQUAL_HINT(a[0].rating, b[0].rating, "Wrong rating find (with rating filter)"s);
    }
    {
        b = {{4, 0.6507, 6}, {12, 0.1014, 1}};
        a = server.FindTopDocuments("пушистый ухоженный кот"s, 
            [](int document_id, DocumentStatus status, int rating) {return status == DocumentStatus::ACTUAL;});
        
        ASSERT_HINT(a.size() == 2u, "Wrong filtration by status"s);
        ASSERT_HINT(abs(a[0].relevance - b[0].relevance) <= EPSILON_TEST, "Wrong relevance by EPSILON_TEST (with status filter)"s);
        ASSERT_HINT(abs(a[1].relevance - b[1].relevance) <= EPSILON_TEST, "Wrong relevance by EPSILON_TEST (with status filter)"s);
        ASSERT_EQUAL_HINT(a[0].rating, b[0].rating, "Wrong rating find (with status filter)"s);
        ASSERT_EQUAL_HINT(a[1].rating, b[1].rating, "Wrong rating find (with status filter)"s);
    }
}

void TestFindTopDocumentsStatus() {
    SearchServer server("и в на"s);
    server.AddDocument(12, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, {1});
    server.AddDocument(4, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {12, 1, 5});
    server.AddDocument(42, "ухоженный пёс выразительные глаза"s, DocumentStatus::BANNED, {-2, 5, 3});

    {
        vector<Document> a = server.FindTopDocuments("пушистый ухоженный кот"s, DocumentStatus::ACTUAL);
        ASSERT_HINT(a.size() == 2u, "Wrong find by status"s);
    }
}

void TestFindTopDocuments() {
    SearchServer server("и в на"s);
    server.AddDocument(12, "белый кот и модный ошейник"s, DocumentStatus::IRRELEVANT, {1});
    server.AddDocument(4, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {12, 1, 5});
    server.AddDocument(42, "ухоженный пёс выразительные глаза"s, DocumentStatus::BANNED, {-2, 5, 3});

    {
        vector<Document> a = server.FindTopDocuments("пушистый ухоженный кот"s);
        ASSERT_HINT(a.size() == 1u, "Wrong find by default"s);
    }
    {
        /* const auto found_wrong_doc = server.FindTopDocuments("кот \x02");
        ASSERT_HINT(!found_wrong_doc.empty(), "Wrong to find non-usable words"s);
        
        const auto found_doc_1 = server.FindTopDocuments("-");
        ASSERT_HINT(!found_doc_1.empty(), "Wrong to find minus"s);

        const auto found_doc_minus = server.FindTopDocuments("кот -");
        ASSERT_HINT(!found_doc_minus.empty(), "Wrong to find minus"s);
        
        const auto found_minmin = server.FindTopDocuments("--кот");
        ASSERT_HINT(!found_minmin.empty(), "Wrong to find --"s); */
    }
}

void TestSortByRelevance() {
    SearchServer server("и в на"s);
    server.AddDocument(12, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, {1});
    server.AddDocument(4, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {12, 1, 5});
    server.AddDocument(42, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {-2, 5, 3});
    
    {
        vector<Document> a = server.FindTopDocuments("пушистый ухоженный кот"s);
        ASSERT_HINT(a.size() == 3u, "Wrong found vector of document size"s);
        ASSERT_HINT((a[0].relevance >= a[1].relevance) && (a[1].relevance >= a[2].relevance), "Wrong sort by relevancet"s);
    }
}

void TestComputingRating() {
    SearchServer server("и в на"s);
    server.AddDocument(12, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, {1});
    server.AddDocument(4, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {12, 1, 5});
    server.AddDocument(42, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {-2, 5, 3});

    vector<Document> a = server.FindTopDocuments("пушистый ухоженный кот"s);
    ASSERT_HINT((a[0].rating == 6) && (a[1].rating == 2) && (a[2].rating == 1), "Wrong rating calculating"s);
}

void TestComputingRelevance() {
    SearchServer server("и в на"s);
    server.AddDocument(12, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, {1});
    server.AddDocument(4, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {12, 1, 5});
    server.AddDocument(42, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {-2, 5, 3});

    vector<Document> a = server.FindTopDocuments("пушистый ухоженный кот"s);
    vector<Document> b = {{4, 0.6507, 6}, {42, 0.2746, 2}, {12, 0.1014, 1}}; 

    ASSERT_EQUAL_HINT(a.size(), b.size(), "Wrong find by default (wrong size of documents found)"s);
    ASSERT_HINT((a[0].relevance - b[0].relevance) <= EPSILON_TEST, "Wrong computing relevance"s);
    ASSERT_HINT((a[1].relevance - b[1].relevance) <= EPSILON_TEST, "Wrong computing relevance"s);
    ASSERT_HINT((a[2].relevance - b[2].relevance) <= EPSILON_TEST, "Wrong computing relevance"s);
    
}

void TestCountOfDocuments() {
    SearchServer server(""s);
    ASSERT_EQUAL_HINT(server.GetDocumentCount(), 0, "Wrong object init(should be o documents)"s);
    server.AddDocument(12, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, {1});
    ASSERT_EQUAL_HINT(server.GetDocumentCount(), 1u, "Wrong counting document"s);
    server.AddDocument(4, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {12, 1, 5});
    ASSERT_EQUAL_HINT(server.GetDocumentCount(), 2u, "Wrong counting document"s);
    server.AddDocument(42, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {-2, 5, 3});
    ASSERT_EQUAL_HINT(server.GetDocumentCount(), 3u, "Wrong counting document"s);
}

void TestGetDocumentId(){
    SearchServer server("и в на"s);
    server.AddDocument(12, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, {1});
    server.AddDocument(4, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, {12, 1, 5});
    server.AddDocument(42, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, {-2, 5, 3});

    {
        int doc_id = server.GetDocumentId(1);
        ASSERT_HINT(doc_id == 4, "Wrong ID"s);
    }
    {
        //int doc_id = server.GetDocumentId(12);
    }
}

void TestSearchServer() {
    RUN_TEST(TestConstructors);
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestMinusWordsExcluded);
    RUN_TEST(TestMatchFunction);
    RUN_TEST(TestFindTopDocumentsPredicate);
    RUN_TEST(TestFindTopDocumentsStatus);
    RUN_TEST(TestFindTopDocuments);
    RUN_TEST(TestSortByRelevance);
    RUN_TEST(TestComputingRating);
    RUN_TEST(TestComputingRelevance);
    RUN_TEST(TestCountOfDocuments);
    RUN_TEST(TestGetDocumentId);

}
// --------- Окончание модульных тестов поисковой системы -----------

int main() {

    TestSearchServer();
    // Если вы видите эту строку, значит все тесты прошли успешно
    cout << "Search server testing finished"s << endl;
    return 0;
}

/* int main() {
    try {
        SearchServer search_server ("и в на"s);
 
        search_server.AddDocument(1, "пушистый кот пушистый хвост", DocumentStatus::ACTUAL, {7, 2, 7});
        search_server.AddDocument(2, "пушистый пёс и модный ошейник", DocumentStatus::ACTUAL, {1, 2});
        search_server.AddDocument(4, "пушистый пёс и модный ошейник", DocumentStatus::ACTUAL, {1, 2});
        search_server.AddDocument(3, "большой пёс скворец", DocumentStatus::ACTUAL, {1, 3, 2});
 
        const auto documents = search_server.FindTopDocuments("--пушистый"); 
        for (const Document& document : documents) {
            PrintDocument(document);
        }
    }
    catch(std::invalid_argument& except){
        std::cout << except.what()<< std::endl;
    }
 
    catch(std::out_of_range& except){
        std::cout << "ID is wrong!" << std::endl;
    }
} */