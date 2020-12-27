#ifndef SequenceNumberUtils_h
#define SequenceNumberUtils_h

namespace RTC
{

// -----------------------------------------------------------
// File: webrtc/src/rtc_base/numerics/mod_ops.h
// -----------------------------------------------------------

// Calculates the forward difference between two wrapping numbers.
//
// Example:
// uint8_t x = 253;
// uint8_t y = 2;
//
// ForwardDiff(x, y) == 5
//
//   252   253   254   255    0     1     2     3
// #################################################
// |     |  x  |     |     |     |     |  y  |     |
// #################################################
//          |----->----->----->----->----->
//
// ForwardDiff(y, x) == 251
//
//   252   253   254   255    0     1     2     3
// #################################################
// |     |  x  |     |     |     |     |  y  |     |
// #################################################
// -->----->                              |----->---
//
// If M > 0 then wrapping occurs at M, if M == 0 then wrapping occurs at the
// largest value representable by T.
template <typename T, T M>
inline typename std::enable_if<(M > 0), T>::type ForwardDiff(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  //RTC_DCHECK_LT(a, M);
  //RTC_DCHECK_LT(b, M);
  return a <= b ? b - a : M - (a - b);
}

template <typename T, T M>
inline typename std::enable_if<(M == 0), T>::type ForwardDiff(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  return b - a;
}

template <typename T>
inline T ForwardDiff(T a, T b) {
  return ForwardDiff<T, 0>(a, b);
}

template <typename T, T M>
inline typename std::enable_if<(M == 0), T>::type ReverseDiff(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  return a - b;
}

template <typename T>
inline T ReverseDiff(T a, T b) {
  return ReverseDiff<T, 0>(a, b);
}

// Calculates the minimum distance between to wrapping numbers.
//
// The minimum distance is defined as min(ForwardDiff(a, b), ReverseDiff(a, b))
template <typename T, T M = 0>
inline T MinDiff(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  return std::min(ForwardDiff<T, M>(a, b), ReverseDiff<T, M>(a, b));
}

// -----------------------------------------------------------
// File: webrtc/src/rtc_base/numerics/sequence_number_util.h
// -----------------------------------------------------------

// Test if the sequence number |a| is ahead or at sequence number |b|.
//
// If |M| is an even number and the two sequence numbers are at max distance
// from each other, then the sequence number with the highest value is
// considered to be ahead.
template <typename T, T M>
inline typename std::enable_if<(M > 0), bool>::type AheadOrAt(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  const T maxDist = M / 2;
  if (!(M & 1) && MinDiff<T, M>(a, b) == maxDist)
    return b < a;
  return ForwardDiff<T, M>(b, a) <= maxDist;
}

template <typename T, T M>
inline typename std::enable_if<(M == 0), bool>::type AheadOrAt(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  const T maxDist = std::numeric_limits<T>::max() / 2 + T(1);
  if (a - b == maxDist)
    return b < a;
  return ForwardDiff(b, a) < maxDist;
}

template <typename T>
inline bool AheadOrAt(T a, T b) {
  return AheadOrAt<T, 0>(a, b);
}

// Test if the sequence number |a| is ahead of sequence number |b|.
//
// If |M| is an even number and the two sequence numbers are at max distance
// from each other, then the sequence number with the highest value is
// considered to be ahead.
template <typename T, T M = 0>
inline bool AheadOf(T a, T b) {
  static_assert(std::is_unsigned<T>::value,
                "Type must be an unsigned integer.");
  return a != b && AheadOrAt<T, M>(a, b);
}

// Comparator used to compare sequence numbers in a continuous fashion.
//
// WARNING! If used to sort sequence numbers of length M then the interval
//          covered by the sequence numbers may not be larger than floor(M/2).
template <typename T, T M = 0>
struct AscendingSeqNumComp {
  bool operator()(T a, T b) const { return AheadOf<T, M>(a, b); }
};

// Comparator used to compare sequence numbers in a continuous fashion.
//
// WARNING! If used to sort sequence numbers of length M then the interval
//          covered by the sequence numbers may not be larger than floor(M/2).
template <typename T, T M = 0>
struct DescendingSeqNumComp {
  bool operator()(T a, T b) const { return AheadOf<T, M>(b, a); }
};

} // namespace RTC

#endif /* SequenceNumberUtils_h */
