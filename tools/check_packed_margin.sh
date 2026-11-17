#!/bin/sh
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Report a packed root image's size against its block budget and fail when
# the budget is exceeded or less than the required margin is left.
#
# hsaout_blocks (tools/hsaout/libhsaout.c) counts ceil(bytes / 1024) data
# blocks plus one indirect block once the file passes four, so a budget of
# N blocks (N from 6 to 260, where hsaout_blocks counts one indirect block) holds at most
# (N - 1) * 1024 packed bytes. The margin
# is that cap less the packed size: the growth a compiler or a patch may add
# before the image needs another block.
#
# usage: check_packed_margin.sh NAME PACKED_BYTES MAX_BLOCKS MIN_MARGIN_BYTES
set -eu

[ $# -eq 4 ] || { echo "usage: $0 NAME PACKED_BYTES MAX_BLOCKS MIN_MARGIN" >&2; exit 2; }
name=$1
packed=$2
max_blocks=$3
min_margin=$4

for v in "$packed" "$max_blocks" "$min_margin"; do
	case $v in
	"" | *[!0-9]*) echo "$name: non-numeric operand '$v'" >&2; exit 2 ;;
	esac
done
[ "$max_blocks" -gt 5 ] || { echo "$name: block budget must exceed 5" >&2; exit 2; }
# hsaout_blocks counts a second metadata block once the total passes 4 + 256,
# which 260 data blocks plus one indirect block already do, so the formula
# below holds up to a budget of 260 and overstates the cap beyond it; refuse.
[ "$max_blocks" -le 260 ] || { echo "$name: block budget above 260 is outside the single-indirect range" >&2; exit 2; }

cap=$(( (max_blocks - 1) * 1024 ))
margin=$(( cap - packed ))
echo "$name: packed $packed bytes of $cap ($max_blocks blocks), margin $margin bytes, required $min_margin"
if [ "$margin" -lt 0 ]; then
	echo "$name: packed root image exceeds $max_blocks blocks" >&2
	exit 1
fi
if [ "$margin" -lt "$min_margin" ]; then
	echo "$name: margin $margin bytes is under $min_margin; the next growth or a different compiler overflows the budget" >&2
	exit 1
fi
