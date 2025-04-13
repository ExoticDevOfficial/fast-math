#include <iostream>     // Console I/O
#include <vector>       // For storing sequences
#include <string>       // String manipulation
#include <cmath>        // Math functions (log2, ceil, etc.)
#include <chrono>       // High-resolution timing
#include <limits>       // Input validation limits
#include <stdexcept>    // Standard exceptions
#include <iomanip>      // Output formatting (setprecision, setw)
#include <fstream>      // File stream (for JSON output)
#include <filesystem>   // Directory creation (Requires C++17!)
#include <map>          // Algorithm selection
#include <random>       // For Monte Carlo Pi
#include <sstream>      // String stream (for input parsing)
#include <cstdlib>      // For system() command (screen clear)
#include <optional>     // For optional max_val in input getter
#include <thread>       // Included for future potential

// Platform-specific includes/defines for console handling
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define CLEAR_COMMAND "cls"
#else
#include <unistd.h>
#define CLEAR_COMMAND "clear"
#endif

// --- GMP and MPFR Headers ---
#include <gmpxx.h>
#include <mpfr.h>

// --- JSON Library Header ---
#include "json.hpp"
using json = nlohmann::json;

// --- Constants and Global Settings ---
const std::string APP_VERSION = "1.0"; // Final v1.0 Version
const int MIN_ACCURACY = 1;
const int MAX_ACCURACY = 10;
const mpfr_prec_t MIN_MPFR_PREC = 64;

// --- Enums for Clarity ---
enum class OutputType { PRINT_CONSOLE, SAVE_JSON };
enum class CalculationType { PI, FIBONACCI, GOLDEN_RATIO, EULER_NUMBER };
enum class PiAlgorithm { GAUSS_LEGENDRE, BORWEIN_QUARTIC, MACHIN_LIKE, MONTE_CARLO };
enum class FibMode { SEQUENCE, NTH_TERM };
enum class FibAlgorithmSeq { ITERATIVE, BINET };
enum class FibAlgorithmNth { ITERATIVE, MATRIX_EXP };
enum class PhiAlgorithm { DIRECT_FORMULA, FIB_RATIO, CONTINUED_FRACTION };
enum class EAlgorithm { SUM_SERIES, CONTINUED_FRACTION, MPFR_CONST_E };

// --- Parameter Structs ---
struct CalcParams {
    OutputType output_type = OutputType::PRINT_CONSOLE;
    int accuracy_level = 5;
    std::string output_filename_base = "calculation_result";
    json metadata;
    CalcParams() : metadata(json::object()) {
        metadata["app_version"] = APP_VERSION;
        metadata["parameters"] = json::object();
        auto now = std::chrono::system_clock::now();
        auto itt = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&itt), "%Y-%m-%dT%H:%M:%S");
        metadata["timestamp_local"] = ss.str();
    }
    virtual ~CalcParams() = default;
};
struct PiParams : CalcParams { PiAlgorithm algorithm = PiAlgorithm::GAUSS_LEGENDRE; long decimal_places = 100; long mc_iterations = 1000000; };
struct FibParams : CalcParams { FibMode mode = FibMode::SEQUENCE; FibAlgorithmSeq seq_algorithm = FibAlgorithmSeq::ITERATIVE; FibAlgorithmNth nth_algorithm = FibAlgorithmNth::MATRIX_EXP; int terms = 50; long nth_value = 50; };
struct PhiParams : CalcParams { PhiAlgorithm algorithm = PhiAlgorithm::DIRECT_FORMULA; long decimal_places = 100; int ratio_terms = 100; int cf_iterations = 100; };
struct EulerParams : CalcParams { EAlgorithm algorithm = EAlgorithm::MPFR_CONST_E; long decimal_places = 100; int series_terms = 0; int cf_iterations = 100; };

// --- RAII Wrapper for mpfr_t ---
class MpfrVar {
    bool initialized;
public:
    mpfr_t val;
    MpfrVar() : initialized(false) {}
    void init(mpfr_prec_t prec) {
        mpfr_prec_t eff_prec = std::max((mpfr_prec_t)MPFR_PREC_MIN, prec);
        if (!initialized) { mpfr_init2(val, eff_prec); initialized = true; }
        else { mpfr_set_prec(val, eff_prec); }
    }
    ~MpfrVar() { if (initialized) { mpfr_clear(val); } }
    MpfrVar(const MpfrVar&) = delete; MpfrVar& operator=(const MpfrVar&) = delete;
    MpfrVar(MpfrVar&&) = delete; MpfrVar& operator=(MpfrVar&&) = delete;
};

// --- Console Utilities ---
void clear_screen() {
    int result = std::system(CLEAR_COMMAND);
    if (result != 0) { std::cerr << "Warning: Failed to clear screen." << std::endl; std::cout << std::string(5, '\n'); }
}
void setup_console() {
    #ifdef _WIN32
        UINT old_cp_in = GetConsoleCP(); UINT old_cp_out = GetConsoleOutputCP();
        if (!SetConsoleCP(CP_UTF8)) { std::cerr << "Warning: Failed to set console input CP to UTF-8." << std::endl; }
        if (!SetConsoleOutputCP(CP_UTF8)) { std::cerr << "Warning: Failed to set console output CP to UTF-8." << std::endl; }
        else { setvbuf(stdout, nullptr, _IOFBF, 1000); setvbuf(stderr, nullptr, _IOFBF, 1000); }
    #endif
}
void wait_for_enter(const std::string& message = "\nPress Enter to continue...") {
    std::cout << message << std::endl;
    // Safely ignore remaining input buffer BEFORE waiting for new line if needed
    // **NOTE:** This ignore might be needed *before* calling wait_for_enter,
    //           depending on the preceding input method.
    //           However, using getline in input functions often handles this better.
    // std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (std::cin.eof()) return; // Don't wait if input stream ended

    std::string dummy;
    std::getline(std::cin, dummy); // Read the Enter press (and any leftover newline)

    if (std::cin.eof()) return; // Check again after getline attempt
}

// --- Robust Input Functions ---
template <typename T>
T get_numeric_input(const std::string& prompt, T min_val, std::optional<T> max_val = std::nullopt) {
    T value; std::string line;
    while (true) {
        std::cout << prompt;
        if (!std::getline(std::cin, line)) { throw std::runtime_error("Input stream error or EOF."); }
        if (line == "0") { throw std::runtime_error("User requested exit."); }
        std::stringstream ss(line);
        if (ss >> value && ss.eof() && value >= min_val && (!max_val.has_value() || value <= max_val.value())) {
            return value;
        } else {
            std::cerr << "Invalid input. Please enter an integer";
            if (max_val.has_value()) { std::cerr << " between " << min_val << " and " << max_val.value(); }
            else { std::cerr << " >= " << min_val; }
            std::cerr << " (or 0 to Exit)." << std::endl;
        }
    }
}
int get_int_input(const std::string& prompt, int min_val, int max_val) { return get_numeric_input<int>(prompt, min_val, std::optional<int>(max_val)); }
long get_long_input(const std::string& prompt, long min_val) { return get_numeric_input<long>(prompt, min_val, std::nullopt); }
std::string get_string_input(const std::string& prompt, const std::string& default_val = "") {
    std::cout << prompt;
    if (!default_val.empty()) { std::cout << "[" << default_val << "]: "; }
    std::string line;
    if (!std::getline(std::cin, line)) { throw std::runtime_error("Input stream error or EOF."); }
    if (line.empty()) { return default_val; }
    return line;
}

// --- Core Calculation Logic ---

// Precision Calculation
mpfr_prec_t calculate_precision_bits(long decimal_places, int accuracy_level) {
    if (decimal_places < 0) decimal_places = 0;
    double base_bits_d = decimal_places * std::log2(10.0);
    long guard_bits = 16 + (accuracy_level - MIN_ACCURACY) * 8;
    long total_bits_l = static_cast<long>(std::ceil(base_bits_d)) + guard_bits;
    return std::max((long)MIN_MPFR_PREC, total_bits_l);
}

// MPFR to String Conversion (Robust)
std::string mpfr_to_string(mpfr_t val, long decimal_places) {
    if (mpfr_nan_p(val) || mpfr_inf_p(val)) return "[Error: NaN or Inf]";
    if (mpfr_zero_p(val)) { if (decimal_places <= 0) return "0"; std::string z = "0."; z.append(decimal_places, '0'); return z; }
    size_t string_digits_req = std::max(1L, decimal_places) + 5; char *c_str = nullptr; mpfr_exp_t exp = 0;
    c_str = mpfr_get_str(nullptr, &exp, 10, string_digits_req, val, MPFR_RNDN);
    if (!c_str) { std::cerr << "Error: mpfr_get_str failed." << std::endl; return "[Error]"; }
    std::string result_str; bool is_negative = (c_str[0] == '-'); const char *digits_start = is_negative ? (c_str + 1) : c_str;
    if (is_negative) result_str += '-';
    if (exp <= 0) { result_str += "0."; result_str += std::string(std::abs(exp), '0'); result_str += digits_start; }
    else { std::string digits_part(digits_start);
        if (digits_part.length() < static_cast<size_t>(exp)) { result_str += digits_part; result_str += std::string(static_cast<size_t>(exp) - digits_part.length(), '0'); result_str += "."; }
        else { result_str += digits_part.substr(0, exp); result_str += "."; result_str += digits_part.substr(exp); }
    }
    mpfr_free_str(c_str); size_t decimal_point_pos = result_str.find('.');
    if (decimal_point_pos == std::string::npos) { if (decimal_places > 0) { result_str += "."; result_str.append(decimal_places, '0'); } }
    else { size_t current_decimals = result_str.length() - decimal_point_pos - 1; long places_to_add = decimal_places - current_decimals;
        if (places_to_add > 0) { result_str.append(places_to_add, '0'); } else if (places_to_add < 0) { result_str.resize(decimal_point_pos + 1 + decimal_places); }
        if (decimal_places <= 0) { result_str.resize(decimal_point_pos); }
    }
    return result_str;
}


// --- Specific Calculation Algorithm Implementations ---

// --- Pi ---
std::string pi_gauss_legendre(mpfr_prec_t bits, long places) {
    MpfrVar a, b, t, p, a_next, tmp1, tmp2, pi_val;
    a.init(bits); b.init(bits); t.init(bits); p.init(bits); a_next.init(bits);
    tmp1.init(bits); tmp2.init(bits); pi_val.init(bits);
    mpfr_set_ui(a.val, 1, MPFR_RNDN); mpfr_set_ui(tmp1.val, 2, MPFR_RNDN);
    mpfr_sqrt(tmp1.val, tmp1.val, MPFR_RNDN); mpfr_ui_div(b.val, 1, tmp1.val, MPFR_RNDN);
    mpfr_set_d(t.val, 0.25, MPFR_RNDN); mpfr_set_ui(p.val, 1, MPFR_RNDN);
    long num_iterations = static_cast<long>(std::ceil(std::log2(bits)));
    for (long i = 0; i < num_iterations; ++i) {
        mpfr_add(a_next.val, a.val, b.val, MPFR_RNDN); mpfr_div_ui(a_next.val, a_next.val, 2, MPFR_RNDN);
        mpfr_mul(tmp1.val, a.val, b.val, MPFR_RNDN); mpfr_sqrt(b.val, tmp1.val, MPFR_RNDN);
        mpfr_sub(tmp1.val, a.val, a_next.val, MPFR_RNDN); mpfr_sqr(tmp2.val, tmp1.val, MPFR_RNDN);
        mpfr_mul(tmp1.val, p.val, tmp2.val, MPFR_RNDN); mpfr_sub(t.val, t.val, tmp1.val, MPFR_RNDN);
        mpfr_set(a.val, a_next.val, MPFR_RNDN); mpfr_mul_ui(p.val, p.val, 2, MPFR_RNDN);
    }
    mpfr_add(tmp1.val, a.val, b.val, MPFR_RNDN); mpfr_sqr(tmp1.val, tmp1.val, MPFR_RNDN);
    mpfr_mul_ui(tmp2.val, t.val, 4, MPFR_RNDN);
    if (mpfr_zero_p(tmp2.val)) return "[Error: Division by zero]";
    mpfr_div(pi_val.val, tmp1.val, tmp2.val, MPFR_RNDN);
    return mpfr_to_string(pi_val.val, places);
}
std::string pi_borwein_quartic(mpfr_prec_t bits, long places) {
    MpfrVar a, y, alpha, tmp1, tmp2, tmp3, pi_val;
    a.init(bits); y.init(bits); alpha.init(bits); tmp1.init(bits); tmp2.init(bits); tmp3.init(bits); pi_val.init(bits);
    mpfr_set_ui(tmp1.val, 2, MPFR_RNDN); mpfr_sqrt(tmp2.val, tmp1.val, MPFR_RNDN);
    mpfr_sub_ui(y.val, tmp2.val, 1, MPFR_RNDN); mpfr_mul_ui(tmp1.val, tmp2.val, 4, MPFR_RNDN);
    mpfr_ui_sub(a.val, 6, tmp1.val, MPFR_RNDN);
    long num_iterations = static_cast<long>(std::ceil(std::log2(std::log2(static_cast<double>(bits))))); // Need double for inner log2
    for (long i = 0; i < num_iterations; ++i) {
        mpfr_sqr(tmp1.val, y.val, MPFR_RNDN); mpfr_sqr(tmp1.val, tmp1.val, MPFR_RNDN);
        mpfr_ui_sub(tmp2.val, 1, tmp1.val, MPFR_RNDN); mpfr_rootn_ui(tmp1.val, tmp2.val, 4, MPFR_RNDN);
        mpfr_ui_sub(tmp2.val, 1, tmp1.val, MPFR_RNDN); mpfr_add_ui(tmp3.val, tmp1.val, 1, MPFR_RNDN);
        if (mpfr_zero_p(tmp3.val)) return "[Error: Division by zero (Borwein y)]";
        mpfr_div(y.val, tmp2.val, tmp3.val, MPFR_RNDN); mpfr_add_ui(tmp1.val, y.val, 1, MPFR_RNDN);
        mpfr_sqr(tmp2.val, tmp1.val, MPFR_RNDN); mpfr_sqr(tmp2.val, tmp2.val, MPFR_RNDN);
        mpfr_mul(alpha.val, a.val, tmp2.val, MPFR_RNDN); mpz_t p2z; mpz_init(p2z); mpz_ui_pow_ui(p2z, 2, 2 * i + 3);
        mpfr_set_z(tmp1.val, p2z, MPFR_RNDN); mpz_clear(p2z); mpfr_sqr(tmp2.val, y.val, MPFR_RNDN);
        mpfr_add(tmp2.val, tmp2.val, y.val, MPFR_RNDN); mpfr_add_ui(tmp2.val, tmp2.val, 1, MPFR_RNDN);
        mpfr_mul(tmp3.val, y.val, tmp2.val, MPFR_RNDN); mpfr_mul(tmp1.val, tmp1.val, tmp3.val, MPFR_RNDN);
        mpfr_sub(a.val, alpha.val, tmp1.val, MPFR_RNDN);
    }
    if (mpfr_zero_p(a.val)) return "[Error: Division by zero (Borwein final)]";
    mpfr_ui_div(pi_val.val, 1, a.val, MPFR_RNDN);
    return mpfr_to_string(pi_val.val, places);
}
std::string pi_machin_like(mpfr_prec_t bits, long places) {
     MpfrVar atan1_5, atan1_239, term1, term2, pi_val, five, tt39;
     atan1_5.init(bits); atan1_239.init(bits); term1.init(bits); term2.init(bits); pi_val.init(bits); five.init(bits); tt39.init(bits);
     mpfr_set_ui(five.val, 5, MPFR_RNDN); mpfr_set_ui(tt39.val, 239, MPFR_RNDN);
     mpfr_ui_div(term1.val, 1, five.val, MPFR_RNDN); mpfr_atan(atan1_5.val, term1.val, MPFR_RNDN);
     mpfr_ui_div(term2.val, 1, tt39.val, MPFR_RNDN); mpfr_atan(atan1_239.val, term2.val, MPFR_RNDN);
     mpfr_mul_ui(term1.val, atan1_5.val, 16, MPFR_RNDN); mpfr_mul_ui(term2.val, atan1_239.val, 4, MPFR_RNDN);
     mpfr_sub(pi_val.val, term1.val, term2.val, MPFR_RNDN);
    return mpfr_to_string(pi_val.val, places);
}
std::string pi_monte_carlo(long iterations, mpfr_prec_t bits, long places) {
    if (iterations <= 0) return "[Error: Need positive iterations for Monte Carlo]";
    long inside_circle = 0;
    std::mt19937_64 rng(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (long i = 0; i < iterations; ++i) { double x = dist(rng), y = dist(rng); if (x * x + y * y <= 1.0) inside_circle++; }
    MpfrVar pi_approx, mpfr_inside, mpfr_total, four;
    pi_approx.init(bits); mpfr_inside.init(bits); mpfr_total.init(bits); four.init(bits);
    mpfr_set_ui(four.val, 4, MPFR_RNDN); mpfr_set_ui(mpfr_total.val, iterations, MPFR_RNDN); mpfr_set_ui(mpfr_inside.val, inside_circle, MPFR_RNDN);
    if (mpfr_zero_p(mpfr_total.val)) return "[Error: Division by zero (iterations=0)]";
    mpfr_div(pi_approx.val, mpfr_inside.val, mpfr_total.val, MPFR_RNDN);
    mpfr_mul(pi_approx.val, pi_approx.val, four.val, MPFR_RNDN);
    return mpfr_to_string(pi_approx.val, places);
}

// --- Fibonacci ---
std::vector<mpz_class> fib_seq_iterative(int terms) {
    if (terms <= 0) return {}; if (terms == 1) return {0};
    std::vector<mpz_class> sequence;
    try { sequence.reserve(terms); mpz_class a=0, b=1; sequence.push_back(a); if(terms>1) sequence.push_back(b);
          for(int i=2; i<terms; ++i) { mpz_class next=a+b; a=b; b=next; sequence.push_back(b); }
    } catch (const std::bad_alloc&) { std::cerr << "\nError: Out of memory (Fib Iterative Sequence)" << std::endl; /* Return partial */ }
    return sequence;
}
std::vector<mpz_class> fib_seq_binet(int terms, mpfr_prec_t bits) {
     if (terms <= 0) return {}; if (terms == 1) return {0};
    std::vector<mpz_class> sequence;
    MpfrVar phi, psi, sqrt5, num, term_f, one, two, five;
    try { sequence.reserve(terms); phi.init(bits); psi.init(bits); sqrt5.init(bits); num.init(bits);
          term_f.init(bits); one.init(bits); two.init(bits); five.init(bits);
          mpfr_set_ui(one.val, 1, MPFR_RNDN); mpfr_set_ui(two.val, 2, MPFR_RNDN); mpfr_set_ui(five.val, 5, MPFR_RNDN);
          mpfr_sqrt(sqrt5.val, five.val, MPFR_RNDN); mpfr_add(phi.val, one.val, sqrt5.val, MPFR_RNDN);
          mpfr_div(phi.val, phi.val, two.val, MPFR_RNDN); mpfr_sub(psi.val, one.val, sqrt5.val, MPFR_RNDN);
          mpfr_div(psi.val, psi.val, two.val, MPFR_RNDN);
          sequence.push_back(0); if (terms > 1) sequence.push_back(1);
          for (int n = 2; n < terms; ++n) {
             mpfr_pow_ui(term_f.val, phi.val, n, MPFR_RNDN); mpfr_pow_ui(num.val, psi.val, n, MPFR_RNDN);
             mpfr_sub(term_f.val, term_f.val, num.val, MPFR_RNDN); mpfr_div(term_f.val, term_f.val, sqrt5.val, MPFR_RNDN);
             mpz_class fib_n; mpfr_get_z(fib_n.get_mpz_t(), term_f.val, MPFR_RNDN); sequence.push_back(fib_n);
          }
    } catch (const std::bad_alloc&) { std::cerr << "\nError: Out of memory (Fib Binet Sequence)" << std::endl; /* Return partial */ }
    return sequence;
}
mpz_class fib_nth_iterative(long n) {
    if (n <= 0) return 0; if (n == 1) return 1;
    mpz_class a = 0, b = 1;
    try { for (long i = 2; i <= n; ++i) { mpz_class next = a + b; a = b; b = next; } }
    catch(...) { std::cerr << "\nError: Exception during Nth Fib Iterative (N=" << n << ")" << std::endl; return -1; }
    return b;
}
void multiply_fib_matrix(mpz_class F[2][2], mpz_class M[2][2]) {
    mpz_class x = F[0][0] * M[0][0] + F[0][1] * M[1][0]; mpz_class y = F[0][0] * M[0][1] + F[0][1] * M[1][1];
    mpz_class z = F[1][0] * M[0][0] + F[1][1] * M[1][0]; mpz_class w = F[1][0] * M[0][1] + F[1][1] * M[1][1];
    F[0][0] = x; F[0][1] = y; F[1][0] = z; F[1][1] = w;
}
void power_fib_matrix(mpz_class F[2][2], long n) {
    if (n <= 1) return; mpz_class M[2][2] = {{1, 1}, {1, 0}};
    power_fib_matrix(F, n / 2); multiply_fib_matrix(F, F);
    if (n % 2 != 0) { multiply_fib_matrix(F, M); }
}
mpz_class fib_nth_matrix_exp(long n) {
    if (n <= 0) return 0; if (n == 1) return 1;
    mpz_class F[2][2] = {{1, 1}, {1, 0}};
    try { power_fib_matrix(F, n - 1); }
    catch (...) { std::cerr << "\nError: Exception during Nth Fib Matrix (N=" << n << ")" << std::endl; return -1; }
    return F[0][0];
}

// --- Golden Ratio ---
std::string phi_direct_formula(mpfr_prec_t bits, long places) {
    MpfrVar one, two, five, sqrt5, phi;
    one.init(bits); two.init(bits); five.init(bits); sqrt5.init(bits); phi.init(bits);
    mpfr_set_ui(one.val, 1, MPFR_RNDN); mpfr_set_ui(two.val, 2, MPFR_RNDN); mpfr_set_ui(five.val, 5, MPFR_RNDN);
    mpfr_sqrt(sqrt5.val, five.val, MPFR_RNDN); mpfr_add(phi.val, one.val, sqrt5.val, MPFR_RNDN);
    mpfr_div(phi.val, phi.val, two.val, MPFR_RNDN);
    return mpfr_to_string(phi.val, places);
}
std::string phi_fib_ratio(mpfr_prec_t bits, long places, int fib_terms) {
     if (fib_terms < 3) fib_terms = 3;
     std::vector<mpz_class> fibs = fib_seq_iterative(fib_terms + 1);
     if (fibs.size() < static_cast<size_t>(fib_terms) + 1) return "[Error: Fib generation failed for ratio]";
     mpz_class fn = fibs[fib_terms]; mpz_class fn_minus_1 = fibs[fib_terms - 1];
     if (fn_minus_1 == 0) return "[Error: Division by zero in Fib Ratio]";
     MpfrVar phi_approx, mpfr_fn, mpfr_fn_minus_1;
     phi_approx.init(bits); mpfr_fn.init(bits); mpfr_fn_minus_1.init(bits);
     mpfr_set_z(mpfr_fn.val, fn.get_mpz_t(), MPFR_RNDN); mpfr_set_z(mpfr_fn_minus_1.val, fn_minus_1.get_mpz_t(), MPFR_RNDN);
     mpfr_div(phi_approx.val, mpfr_fn.val, mpfr_fn_minus_1.val, MPFR_RNDN);
    return mpfr_to_string(phi_approx.val, places);
}
std::string phi_continued_fraction(mpfr_prec_t bits, long places, int iterations) {
     if (iterations <= 0) iterations = 100;
     MpfrVar phi_approx, one; phi_approx.init(bits); one.init(bits);
     mpfr_set_ui(one.val, 1, MPFR_RNDN); mpfr_set_ui(phi_approx.val, 1, MPFR_RNDN);
     for (int i = 0; i < iterations; ++i) {
         if (mpfr_zero_p(phi_approx.val)) return "[Error: Division by zero in CF]";
         mpfr_div(phi_approx.val, one.val, phi_approx.val, MPFR_RNDN);
         mpfr_add_ui(phi_approx.val, phi_approx.val, 1, MPFR_RNDN);
     }
    return mpfr_to_string(phi_approx.val, places);
}

// --- Euler's Number ---
std::string euler_sum_series(mpfr_prec_t bits, long places) {
    MpfrVar e_val, term, k_mpfr;
    e_val.init(bits); term.init(bits); k_mpfr.init(bits);
    mpfr_set_ui(e_val.val, 1, MPFR_RNDN); mpfr_set_ui(term.val, 1, MPFR_RNDN);
    long k = 1;
    while (true) {
        mpfr_set_ui(k_mpfr.val, k, MPFR_RNDN);
        if (mpfr_zero_p(k_mpfr.val)) break;
        mpfr_div(term.val, term.val, k_mpfr.val, MPFR_RNDN);
        mpfr_exp_t term_exp = mpfr_get_exp(term.val);
        mpfr_add(e_val.val, e_val.val, term.val, MPFR_RNDN);
        if (term_exp < -static_cast<mpfr_exp_t>(bits)) break; // Converged
        k++;
        if (k > (long)(bits * 2) + 100) { // Heuristic safety break
             std::cerr << "Warning: Euler series terms exceed limit for precision " << bits << ". Stopping early." << std::endl;
             break;
         }
    }
    return mpfr_to_string(e_val.val, places);
}
std::string euler_continued_fraction(mpfr_prec_t bits, long places, int iterations) {
    if (iterations <= 0) iterations = 100;
    MpfrVar e_val, term; e_val.init(bits); term.init(bits);
    mpfr_set_inf(e_val.val, 1); // Start inner calculation with 1/inf = 0
    for (int i = iterations; i >= 1; --i) {
         long current_term_val; int pattern_pos = i % 3; int k = (i + 2) / 3;
         if (pattern_pos == 2) current_term_val = 2 * k; else current_term_val = 1;
         mpfr_set_ui(term.val, current_term_val, MPFR_RNDN);
         mpfr_ui_div(e_val.val, 1, e_val.val, MPFR_RNDN);
         mpfr_add(e_val.val, term.val, e_val.val, MPFR_RNDN);
    }
    mpfr_add_ui(e_val.val, e_val.val, 2, MPFR_RNDN); // Add the initial integer part '2'
    return mpfr_to_string(e_val.val, places);
}
std::string euler_mpfr_const(mpfr_prec_t bits, long places) {
    MpfrVar e_val; e_val.init(bits);
    mpfr_const_euler(e_val.val, MPFR_RNDN); // Most reliable way
    return mpfr_to_string(e_val.val, places);
}


// --- Output Functions ---
void display_output(const std::string& calc_name, const std::string& result, const CalcParams& params) {
    std::cout << "\n--- Result [" << calc_name << "] ---" << std::endl;
    if (result == "[Error]" || result.find("[Error:") != std::string::npos) {
        std::cout << "Error during calculation: " << result << std::endl;
    } else { std::cout << result << std::endl; }
    std::cout << "--------------------------" << std::endl;
    std::cout << "Metadata:\n" << params.metadata.dump(2) << std::endl;
}
void display_output(const std::string& calc_name, const std::vector<mpz_class>& sequence, const FibParams& params) {
    std::cout << "\n--- Result [" << calc_name << "] ---" << std::endl;
    size_t actual_terms = sequence.size(); size_t display_limit = 20; size_t terms_threshold = 50;
    if (actual_terms == 0 && params.terms > 0) { std::cout << "(Sequence generation failed or resulted in 0 terms)" << std::endl; }
    else if (actual_terms > terms_threshold && actual_terms > display_limit) {
        std::cout << "(Generated " << actual_terms << "/" << params.terms << " terms. Displaying first/last few)" << std::endl;
        size_t half = display_limit / 2; for (size_t i = 0; i < half && i < actual_terms; ++i) std::cout << sequence[i] << ", ";
        std::cout << "..., "; size_t start_last = (actual_terms > half) ? (actual_terms - half) : 0;
        for (size_t i = start_last; i < actual_terms; ++i) std::cout << sequence[i] << (i == actual_terms - 1 ? "" : ", "); std::cout << std::endl;
    } else { std::cout << "(Generated " << actual_terms << "/" << params.terms << " terms)" << std::endl;
        for (size_t i = 0; i < actual_terms; ++i) std::cout << sequence[i] << (i == actual_terms - 1 ? "" : ", "); std::cout << std::endl; }
    std::cout << "--------------------------" << std::endl;
    std::cout << "Metadata:\n" << params.metadata.dump(2) << std::endl;
}
void display_output(const std::string& calc_name, const mpz_class& value, const FibParams& params) {
    std::cout << "\n--- Result [" << calc_name << "] ---" << std::endl;
    if (params.metadata.value("result", "") == "[Error]" || value == -1) { std::cout << "[Error during calculation]" << std::endl; }
    else { std::cout << "F(" << params.nth_value << ") = " << value << std::endl; }
    std::cout << "--------------------------" << std::endl;
    std::cout << "Metadata:\n" << params.metadata.dump(2) << std::endl;
}
bool save_json_output(const json& data, const std::string& base_filename) {
    std::filesystem::path output_dir = "output";
    auto now = std::chrono::system_clock::now(); auto epoch_s = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string filename = base_filename + "_" + std::to_string(epoch_s) + ".json"; std::filesystem::path file_path = output_dir / filename;
    try { if (!std::filesystem::exists(output_dir)) { if (std::filesystem::create_directory(output_dir)) { std::cout << "Created output directory: " << output_dir.string() << std::endl; } else { std::cerr << "Error: Failed to create output directory: " << output_dir.string() << std::endl; return false; } }
        std::ofstream ofs(file_path); if (!ofs.is_open()) { std::cerr << "Error: Failed to open file: " << file_path.string() << std::endl; return false; }
        ofs << std::setw(4) << data << std::endl; ofs.close(); std::cout << "Result saved to: " << file_path.string() << std::endl; return true;
    } catch (const std::exception& e) { std::cerr << "Error saving JSON: " << e.what() << std::endl; return false; }
}

// --- Static Content Pages ---
void display_about() {
    clear_screen();
    std::cout << "--- About ---" << std::endl;
    std::cout << "High-Performance Math Calculator v" << APP_VERSION << std::endl;
    std::cout << "Calculates mathematical constants and sequences with high precision." << std::endl;
    std::cout << "Developed using C++, GMP, MPFR, and nlohmann/json." << std::endl;
    std::cout << "\nFeatures:" << std::endl;
    std::cout << " - Pi (π), Fibonacci, Golden Ratio (φ), Euler's Number (e)" << std::endl;
    std::cout << " - Multiple algorithms for comparison & accuracy" << std::endl;
    std::cout << " - Adjustable accuracy level (influences precision & speed)" << std::endl;
    std::cout << " - Console output or JSON file export to './output/'" << std::endl;
    std::cout << "\nNote: Console symbol display (π, φ) depends on terminal font." << std::endl;
    wait_for_enter();
}
void display_credits() {
    clear_screen();
    std::cout << "--- Credits & Libraries ---" << std::endl;
    std::cout << "Libraries Used:" << std::endl;
    std::cout << " * GMP:   https://gmplib.org/" << std::endl;
    std::cout << " * MPFR:  https://www.mpfr.org/" << std::endl;
    std::cout << " * JSON:  https://github.com/nlohmann/json" << std::endl;
    std::cout << "\nAlgorithms inspired by standard mathematical texts and online resources." << std::endl;
    std::cout << "Developed by Michael." << std::endl;
    wait_for_enter();
}

// --- Main Execution Logic ---
int main() {
    setup_console(); // Attempt console setup first

    // Main application loop
    while (true) {
        clear_screen();
        std::cout << "--- High-Performance Math Calculator v" << APP_VERSION << " ---" << std::endl;
        std::cout << "\n=== Main Menu (Enter 0 to Exit) ===" << std::endl;
        std::cout << "1: Calculate Pi (" << u8"π" << ")" << std::endl;
        std::cout << "2: Calculate Fibonacci" << std::endl;
        std::cout << "3: Calculate Golden Ratio (" << u8"φ" << ")" << std::endl;
        std::cout << "4: Calculate Euler's Number (e)" << std::endl;
        std::cout << "5: About" << std::endl;
        std::cout << "6: Credits" << std::endl;

        // --- Declare variables needed outside the main try block ---
        int main_choice = 0;
        CalculationType calculation_type; // Declared here
        std::string calc_name_str = "Unknown"; // Declared here
        // Use unique_ptr for polymorphic behavior - start with base
        std::unique_ptr<CalcParams> current_params_ptr = std::make_unique<CalcParams>();
        std::chrono::duration<double> calculation_duration(0.0);
        bool error_occurred = false;
        std::string result_str; // For Pi/Phi/Euler results
        std::vector<mpz_class> fib_seq_result; // For Fib sequence
        mpz_class fib_nth_result = -1; // For Fib Nth term (-1 indicates error/not run)
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time, end_time; // Declare time points here


        try { // Wrap menu interaction and calculation setup/execution
            main_choice = get_int_input("Select option (1-6): ", 0, 6);
            // get_int_input throws std::runtime_error("User requested exit.") if '0' is entered

            if (main_choice == 5) { display_about(); continue; } // Loop back to main menu
            if (main_choice == 6) { display_credits(); continue; } // Loop back to main menu

            calculation_type = static_cast<CalculationType>(main_choice - 1); // Assign here

            // --- Parameter Gathering ---
            clear_screen();
            std::cout << "--- Calculation Settings (Enter 0 to Exit/Cancel) ---" << std::endl;

            // 1. Output Type
            std::cout << "1: Print to Console\n2: Save to JSON file" << std::endl;
            int output_choice = get_int_input("Select output method (1-2): ", 0, 2);
            current_params_ptr->output_type = (output_choice == 1) ? OutputType::PRINT_CONSOLE : OutputType::SAVE_JSON;

            if (current_params_ptr->output_type == OutputType::SAVE_JSON) {
                std::string fname = get_string_input("Enter base filename for JSON ", "calculation_result");
                current_params_ptr->output_filename_base = fname;
                current_params_ptr->metadata["parameters"]["output_filename_base"] = fname;
            }

            // 2. Accuracy Level
            current_params_ptr->accuracy_level = get_int_input("Enter accuracy level (1=Fast/Low, 10=Slow/High): ", 1, MAX_ACCURACY);
            current_params_ptr->metadata["parameters"]["accuracy_setting"] = current_params_ptr->accuracy_level;

            // 3. Calculation-Specific Settings & Algorithm + Execution
            clear_screen();
            start_time = std::chrono::high_resolution_clock::now(); // Start timing before specific setup

            switch (calculation_type) { // Switch now uses the correctly scoped variable
            case CalculationType::PI: {
                // CORRECT unique_ptr handling: Create derived, copy base, move ownership
                auto params_pi_ptr = std::make_unique<PiParams>();
                params_pi_ptr->output_type = current_params_ptr->output_type;
                params_pi_ptr->accuracy_level = current_params_ptr->accuracy_level;
                params_pi_ptr->output_filename_base = current_params_ptr->output_filename_base;
                params_pi_ptr->metadata = current_params_ptr->metadata; // Copy base metadata
                current_params_ptr = std::move(params_pi_ptr); // current_params_ptr now owns PiParams
                auto& params = static_cast<PiParams&>(*current_params_ptr); // Use reference

                params.metadata["calculation"] = "Pi"; calc_name_str = "Pi (" + std::string(u8"π") + ")";
                std::cout << "--- Pi (" << u8"π" << ") Calculation ---" << std::endl;
                std::cout << "1: Gauss-Legendre (Fastest Practical, Accurate) (Recommended)" << std::endl;
                std::cout << "2: Borwein Quartic (Very Fast, Accurate)" << std::endl;
                std::cout << "3: Machin-Like (arctan) (Slower, Accurate)" << std::endl;
                std::cout << "4: Monte Carlo (Very Slow, Low Accuracy - Demo Only)" << std::endl;
                int algo_choice = get_int_input("Select algorithm (1-4): ", 0, 4); // *** THIS PROMPT SHOULD NOW APPEAR ***
                params.algorithm = static_cast<PiAlgorithm>(algo_choice - 1);
                params.decimal_places = get_long_input("Enter decimal places (>=0): ", 0);
                params.metadata["parameters"]["decimal_places"] = params.decimal_places;
                std::map<PiAlgorithm, std::string> names = { {PiAlgorithm::GAUSS_LEGENDRE, "Gauss-Legendre"},{PiAlgorithm::BORWEIN_QUARTIC, "Borwein Quartic"},{PiAlgorithm::MACHIN_LIKE, "Machin-Like (arctan)"},{PiAlgorithm::MONTE_CARLO, "Monte Carlo"} };
                params.metadata["parameters"]["algorithm"] = names[params.algorithm];
                if (params.algorithm == PiAlgorithm::MONTE_CARLO) { params.mc_iterations = get_long_input("Enter MC iterations (>0): ", 1); params.metadata["parameters"]["mc_iterations"] = params.mc_iterations; }

                start_time = std::chrono::high_resolution_clock::now(); // Re-time just before call
                mpfr_prec_t bits = calculate_precision_bits(params.decimal_places, params.accuracy_level);
                if (params.algorithm == PiAlgorithm::MONTE_CARLO) bits = std::max((long)MIN_MPFR_PREC, 256L + params.accuracy_level * 32);
                std::cout << "\nCalculating Pi using " << params.metadata["parameters"]["algorithm"].get<std::string>() << "..." << std::endl;
                if (params.algorithm == PiAlgorithm::GAUSS_LEGENDRE) result_str = pi_gauss_legendre(bits, params.decimal_places);
                else if (params.algorithm == PiAlgorithm::BORWEIN_QUARTIC) result_str = pi_borwein_quartic(bits, params.decimal_places);
                else if (params.algorithm == PiAlgorithm::MACHIN_LIKE) result_str = pi_machin_like(bits, params.decimal_places);
                else result_str = pi_monte_carlo(params.mc_iterations, bits, params.decimal_places);
                end_time = std::chrono::high_resolution_clock::now();
                calculation_duration = end_time - start_time;
                params.metadata["result"] = result_str;
                break;
            }
            case CalculationType::FIBONACCI: {
                auto params_fib_ptr = std::make_unique<FibParams>();
                params_fib_ptr->output_type = current_params_ptr->output_type; params_fib_ptr->accuracy_level = current_params_ptr->accuracy_level; params_fib_ptr->output_filename_base = current_params_ptr->output_filename_base; params_fib_ptr->metadata = current_params_ptr->metadata;
                current_params_ptr = std::move(params_fib_ptr);
                auto& params = static_cast<FibParams&>(*current_params_ptr);
                // ... (rest of Fibonacci setup and calculation calls exactly as before) ...
                // ... (Ensure start_time/end_time wrap calculation call) ...
                params.metadata["calculation"] = "Fibonacci"; calc_name_str = "Fibonacci";
                std::cout << "--- Fibonacci Calculation ---" << std::endl;
                std::cout << "1: Generate Sequence\n2: Calculate Nth Term Only" << std::endl;
                int mode_choice = get_int_input("Select mode (1-2): ", 0, 2);
                params.mode = (mode_choice == 1) ? FibMode::SEQUENCE : FibMode::NTH_TERM;
                params.metadata["parameters"]["mode"] = (params.mode == FibMode::SEQUENCE) ? "Sequence" : "Nth Term";
                clear_screen();
                if (params.mode == FibMode::SEQUENCE) {
                    std::cout << "-- Sequence Algorithms --" << std::endl;
                    std::cout << "1: Iterative (Fastest, Accurate) (Recommended)" << std::endl;
                    std::cout << "2: Binet's Formula (Slow, Floating-Point - Low Accuracy)" << std::endl;
                    int algo_choice = get_int_input("Select algorithm (1-2): ", 0, 2);
                    params.seq_algorithm = (algo_choice == 1) ? FibAlgorithmSeq::ITERATIVE : FibAlgorithmSeq::BINET;
                    params.metadata["parameters"]["algorithm"] = (params.seq_algorithm == FibAlgorithmSeq::ITERATIVE) ? "Iterative" : "Binet's Formula";
                    params.terms = static_cast<int>(get_long_input("Enter number of terms (>=1): ", 1));
                    params.metadata["parameters"]["terms"] = params.terms;
                    start_time = std::chrono::high_resolution_clock::now();
                    std::cout << "\nCalculating Fibonacci Sequence..." << std::endl;
                    if (params.seq_algorithm == FibAlgorithmSeq::ITERATIVE) fib_seq_result = fib_seq_iterative(params.terms);
                    else { mpfr_prec_t bits = calculate_precision_bits(params.terms / 4, params.accuracy_level); fib_seq_result = fib_seq_binet(params.terms, std::max((long)MIN_MPFR_PREC, (long)bits)); params.metadata["notes"] = "Binet accuracy depends on MPFR precision."; }
                    end_time = std::chrono::high_resolution_clock::now(); calculation_duration = end_time - start_time;
                    json res_array = json::array(); for (const auto& v : fib_seq_result) res_array.push_back(v.get_str()); params.metadata["result"] = res_array;
                }
                else { // Nth Term
                    std::cout << "-- Nth Term Algorithms --" << std::endl;
                    std::cout << "1: Matrix Exponentiation (Fastest for large N, Accurate) (Recommended)" << std::endl;
                    std::cout << "2: Iterative (Slower for large N, Accurate)" << std::endl;
                    int algo_choice = get_int_input("Select algorithm (1-2): ", 0, 2);
                    params.nth_algorithm = (algo_choice == 1) ? FibAlgorithmNth::MATRIX_EXP : FibAlgorithmNth::ITERATIVE;
                    params.metadata["parameters"]["algorithm"] = (params.nth_algorithm == FibAlgorithmNth::MATRIX_EXP) ? "Matrix Exponentiation" : "Iterative (Nth)";
                    params.nth_value = get_long_input("Enter N (term number >=0): ", 0);
                    params.metadata["parameters"]["n"] = params.nth_value;
                    start_time = std::chrono::high_resolution_clock::now();
                    std::cout << "\nCalculating Fibonacci Nth Term..." << std::endl;
                    if (params.nth_algorithm == FibAlgorithmNth::MATRIX_EXP) fib_nth_result = fib_nth_matrix_exp(params.nth_value);
                    else fib_nth_result = fib_nth_iterative(params.nth_value);
                    end_time = std::chrono::high_resolution_clock::now(); calculation_duration = end_time - start_time;
                    if (fib_nth_result != -1) params.metadata["result"] = fib_nth_result.get_str(); else { params.metadata["result"] = "[Error]"; error_occurred = true; }
                }
                break;
            }
            case CalculationType::GOLDEN_RATIO: {
                auto params_phi_ptr = std::make_unique<PhiParams>();
                *params_phi_ptr = static_cast<PhiParams&>(*current_params_ptr);
                current_params_ptr = std::move(params_phi_ptr);
                auto& params = static_cast<PhiParams&>(*current_params_ptr);
                // ... (rest of Golden Ratio setup and calculation calls exactly as before) ...
                // ... (Ensure start_time/end_time wrap calculation call) ...
                params.metadata["calculation"] = "Golden Ratio"; calc_name_str = "Golden Ratio (" + std::string(u8"φ") + ")";
                std::cout << "--- Golden Ratio (" << u8"φ" << ") Calculation ---" << std::endl;
                std::cout << "1: Direct Formula (Fastest, Accurate) (Recommended)" << std::endl;
                std::cout << "2: Fibonacci Ratio Limit (Slower, Approx. Accurate)" << std::endl;
                std::cout << "3: Continued Fraction (Iterative, Accurate)" << std::endl;
                int algo_choice = get_int_input("Select algorithm (1-3): ", 0, 3);
                params.algorithm = static_cast<PhiAlgorithm>(algo_choice - 1);
                params.decimal_places = get_long_input("Enter decimal places (>=0): ", 0);
                params.metadata["parameters"]["decimal_places"] = params.decimal_places;
                std::map<PhiAlgorithm, std::string> names = { {PhiAlgorithm::DIRECT_FORMULA, "Direct Formula"},{PhiAlgorithm::FIB_RATIO, "Fibonacci Ratio"},{PhiAlgorithm::CONTINUED_FRACTION, "Continued Fraction"} };
                params.metadata["parameters"]["algorithm"] = names[params.algorithm];
                mpfr_prec_t bits = calculate_precision_bits(params.decimal_places, params.accuracy_level);
                if (params.algorithm == PhiAlgorithm::FIB_RATIO) { params.ratio_terms = static_cast<int>(get_long_input("Enter # Fib terms for ratio (>=3): ", 3)); params.metadata["parameters"]["ratio_terms"] = params.ratio_terms; }
                else if (params.algorithm == PhiAlgorithm::CONTINUED_FRACTION) { params.cf_iterations = static_cast<int>(get_long_input("Enter # iterations for fraction (>=1): ", 1)); params.metadata["parameters"]["cf_iterations"] = params.cf_iterations; }
                start_time = std::chrono::high_resolution_clock::now();
                std::cout << "\nCalculating Golden Ratio using " << params.metadata["parameters"]["algorithm"].get<std::string>() << "..." << std::endl;
                if (params.algorithm == PhiAlgorithm::DIRECT_FORMULA) result_str = phi_direct_formula(bits, params.decimal_places);
                else if (params.algorithm == PhiAlgorithm::FIB_RATIO) result_str = phi_fib_ratio(bits, params.decimal_places, params.ratio_terms);
                else result_str = phi_continued_fraction(bits, params.decimal_places, params.cf_iterations);
                end_time = std::chrono::high_resolution_clock::now(); calculation_duration = end_time - start_time;
                params.metadata["result"] = result_str;
                break;
            }
            case CalculationType::EULER_NUMBER: {
                auto params_e_ptr = std::make_unique<EulerParams>();
                *params_e_ptr = static_cast<EulerParams&>(*current_params_ptr);
                current_params_ptr = std::move(params_e_ptr);
                auto& params = static_cast<EulerParams&>(*current_params_ptr);
                // ... (rest of Euler setup and calculation calls exactly as before) ...
                // ... (Ensure start_time/end_time wrap calculation call) ...
                params.metadata["calculation"] = "Euler's Number"; calc_name_str = "Euler's Number (e)";
                std::cout << "--- Euler's Number (e) Calculation ---" << std::endl;
                std::cout << "1: MPFR Built-in Constant (Fastest, Accurate) (Recommended)" << std::endl;
                std::cout << "2: Sum Series (1/k!) (Fast, Accurate)" << std::endl;
                std::cout << "3: Continued Fraction (Iterative, Accurate)" << std::endl;
                int algo_choice = get_int_input("Select algorithm (1-3): ", 0, 3);
                params.algorithm = static_cast<EAlgorithm>(algo_choice - 1);
                params.decimal_places = get_long_input("Enter decimal places (>=0): ", 0);
                params.metadata["parameters"]["decimal_places"] = params.decimal_places;
                std::map<EAlgorithm, std::string> names = { {EAlgorithm::MPFR_CONST_E, "MPFR Built-in"},{EAlgorithm::SUM_SERIES, "Sum Series (1/k!)"},{EAlgorithm::CONTINUED_FRACTION, "Continued Fraction"} };
                params.metadata["parameters"]["algorithm"] = names[params.algorithm];
                mpfr_prec_t bits = calculate_precision_bits(params.decimal_places, params.accuracy_level);
                if (params.algorithm == EAlgorithm::CONTINUED_FRACTION) { params.cf_iterations = static_cast<int>(get_long_input("Enter # iterations for fraction (>=1): ", 1)); params.metadata["parameters"]["cf_iterations"] = params.cf_iterations; }
                start_time = std::chrono::high_resolution_clock::now();
                std::cout << "\nCalculating Euler's Number using " << params.metadata["parameters"]["algorithm"].get<std::string>() << "..." << std::endl;
                if (params.algorithm == EAlgorithm::MPFR_CONST_E) result_str = euler_mpfr_const(bits, params.decimal_places);
                else if (params.algorithm == EAlgorithm::SUM_SERIES) result_str = euler_sum_series(bits, params.decimal_places);
                else result_str = euler_continued_fraction(bits, params.decimal_places, params.cf_iterations);
                end_time = std::chrono::high_resolution_clock::now(); calculation_duration = end_time - start_time;
                params.metadata["result"] = result_str;
                break;
            }
            } // End switch

            // Check for calculation function error strings AFTER the switch
            if (!result_str.empty() && (result_str == "[Error]" || result_str.find("[Error:") != std::string::npos)) { error_occurred = true; }
            // Check Fib Nth term error indicator
            if (calculation_type == CalculationType::FIBONACCI) { // Check type first
                // Safely check if current_params_ptr points to FibParams
                if (auto* fib_params_ptr = dynamic_cast<FibParams*>(current_params_ptr.get())) {
                    if (fib_params_ptr->mode == FibMode::NTH_TERM && fib_nth_result == -1) {
                        error_occurred = true;
                    }
                }
            }


        }
        catch (const std::runtime_error& e) { // Catch user exit or input stream errors
            std::string err_what = e.what();
            if (err_what == "User requested exit.") { std::cout << "\nOperation cancelled by user." << std::endl; }
            else { std::cerr << "\nInput Error: " << err_what << std::endl; }
            error_occurred = true;
            current_params_ptr->metadata["status"] = (err_what == "User requested exit.") ? "Cancelled" : "Error";
            if (err_what != "User requested exit.") current_params_ptr->metadata["error_message"] = "Input stream error or invalid input.";
            // Don't try to capture end_time here as start_time might not be valid if error was early
            calculation_duration = std::chrono::duration<double>(0.0); // Indicate negligible time if error was in setup
        }
        catch (const std::exception& e) { // Catch other standard exceptions
            std::cerr << "\nAn unexpected error occurred: " << e.what() << std::endl;
            error_occurred = true;
            current_params_ptr->metadata["status"] = "Error";
            current_params_ptr->metadata["error_message"] = e.what();
            // Don't try to capture end_time here
            calculation_duration = std::chrono::duration<double>(0.0);
        }

        // --- Post-Calculation Processing ---
        if (!error_occurred && !current_params_ptr->metadata.contains("status")) current_params_ptr->metadata["status"] = "Success";
        // Store the final calculated duration (could be 0 if error was early)
        current_params_ptr->metadata["timing_seconds"] = calculation_duration.count();

        // --- Output Phase ---
        clear_screen(); // Clear before showing final result/status
        if (!error_occurred) {
            // Use dynamic_cast for safer access to derived members, or rely on metadata
            const auto& base_ref = *current_params_ptr; // Use base reference

            if (base_ref.output_type == OutputType::PRINT_CONSOLE) {
                // Use the calculation_type variable that is now correctly scoped
                if (calculation_type == CalculationType::FIBONACCI) {
                    // Must cast to access mode, do it safely
                    if (const auto* fib_params_ptr = dynamic_cast<const FibParams*>(&base_ref)) {
                        if (fib_params_ptr->mode == FibMode::SEQUENCE) {
                            display_output(calc_name_str, fib_seq_result, *fib_params_ptr);
                        }
                        else {
                            display_output(calc_name_str + " Nth Term", fib_nth_result, *fib_params_ptr);
                        }
                    }
                    else { error_occurred = true; std::cerr << "Internal error: Failed cast for Fib output." << std::endl; } // Should not happen
                }
                else { // Pi, Phi, or Euler
                    display_output(calc_name_str, result_str, base_ref);
                }
                if (!error_occurred) wait_for_enter(); // Pause only after successful console output
                else wait_for_enter("Press Enter to return to main menu..."); // Pause after error display too

            }
            else { // JSON Output
                if (!save_json_output(base_ref.metadata, base_ref.output_filename_base)) {
                    std::cerr << "Critical Error: Failed to save JSON output." << std::endl;
                    wait_for_enter("Press Enter to acknowledge error...");
                }
                // No pause needed after JSON save unless debugging
                // std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        else { // Error occurred or cancelled
            std::cout << "\n--- Calculation Failed or Cancelled ---" << std::endl;
            if (current_params_ptr->metadata.contains("error_message")) std::cout << "Error: " << current_params_ptr->metadata["error_message"] << std::endl;
            else if (current_params_ptr->metadata.value("status", "") == "Cancelled") std::cout << "Operation was cancelled by user." << std::endl;
            else std::cout << "An unspecified error occurred during parameter setup or calculation." << std::endl;
            // Display timing only if it's non-zero (meaning calculation phase was likely reached)
            if (current_params_ptr->metadata.value("timing_seconds", 0.0) > 0)
                std::cout << "Timing up to point of failure/cancel: " << std::fixed << std::setprecision(6) << current_params_ptr->metadata.value("timing_seconds", 0.0) << " seconds" << std::endl;
            if (current_params_ptr->output_type == OutputType::SAVE_JSON) {
                std::cerr << "Saving error details to JSON..." << std::endl;
                save_json_output(current_params_ptr->metadata, current_params_ptr->output_filename_base + "_error");
            }
            wait_for_enter("Press Enter to return to main menu..."); // Always pause after error display
        }
        // Loop back to main menu

    } // End main while loop

    std::cout << "\nExiting Calculator v" << APP_VERSION << ". Final MPFR cleanup..." << std::endl;
    mpfr_free_cache(); // Clean up MPFR global cache before exiting
    return 0;
}