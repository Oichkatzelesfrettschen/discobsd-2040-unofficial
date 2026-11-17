/-
Copyright (c) 2026 GizmoBSD

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-/

import Std

/- Specification-level invariants. These do not verify Python, OpenSSL,
   the filesystem, or RP2040 hardware. No cryptographic hardness axiom is used. -/
namespace DiscoBSDRelease

def accepts (classical pq digest geometry : Bool) (sequence floor : Nat) : Bool :=
  classical && pq && digest && geometry && decide (floor ≤ sequence)

theorem requires_pq (a b c d : Bool) (s f : Nat)
    (h : accepts a b c d s f = true) : b = true := by
  simp_all [accepts]

theorem requires_classical (a b c d : Bool) (s f : Nat)
    (h : accepts a b c d s f = true) : a = true := by
  simp_all [accepts]

theorem refuses_rollback (a b c d : Bool) (s f : Nat)
    (h : s < f) : accepts a b c d s f = false := by
  have hn : ¬ f ≤ s := by omega
  simp [accepts, hn]

theorem floor_never_decreases (floor incoming : Nat) : floor ≤ max floor incoming :=
  Nat.le_max_left _ _

theorem admitted_sequence_is_new_floor (s f : Nat) (h : f ≤ s) : max f s = s :=
  Nat.max_eq_right h

-- Python's fixed header is 74 bytes, and its five fixed records are 77 each.
theorem record_in_bounds (i : Nat) (h : i < 5) : 74 + 77*i + 77 ≤ 459 := by
  omega

theorem distinct_record_offsets (i j : Nat) (h : i < j) :
    74 + 77*i + 77 ≤ 74 + 77*j := by
  omega

-- Generic nonnegative penalty lower bound used by the graph cardinality model.
theorem penalty_dominates (cut total violation : Nat) (h : 0 < violation) :
    total < cut + (total + 1)*violation := by
  have hm : total + 1 ≤ (total + 1)*violation := by
    simpa using Nat.mul_le_mul_left (total + 1) h
  omega

end DiscoBSDRelease
