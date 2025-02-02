/**
 * @file
 * This file is part of SeisSol.
 *
 * @author Carsten Uphoff (c.uphoff AT tum.de, http://www5.in.tum.de/wiki/index.php/Carsten_Uphoff,_M.Sc.)
 *
 * @section LICENSE
 * Copyright (c) 2017, SeisSol Group
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @section DESCRIPTION
 */

#ifndef MONITORING_LOOPSTATISTICS_H_
#define MONITORING_LOOPSTATISTICS_H_

#include <cassert>
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <iomanip>
#include <time.h>
#include <vector>

#ifdef USE_MPI
#include <mpi.h>
#endif

namespace seissol {
class LoopStatistics {
public:
  void addRegion(std::string const& name, bool includeInSummary = true) {
    m_regions.push_back(name);
    m_begin.push_back(timespec{});
    m_times.emplace_back();
    m_includeInSummary.push_back(includeInSummary);
  }
  
  unsigned getRegion(std::string const& name) {
    auto first = m_regions.cbegin();
    auto it = std::find(first, m_regions.cend(), name);
    assert(it != m_regions.end());
    return std::distance(first, it);
  }
  
  void begin(unsigned region) {
    clock_gettime(CLOCK_MONOTONIC, &m_begin[region]);
  }
  
  void end(unsigned region, unsigned numIterations, unsigned subRegion) {
    Sample sample;
    clock_gettime(CLOCK_MONOTONIC, &sample.end);
    sample.begin = m_begin[region];
    sample.numIters = numIterations;
    sample.subRegion = subRegion;
    m_times[region].push_back(sample);
  }

  void addSample(unsigned region, unsigned numIters, unsigned subRegion,
                 timespec begin, timespec end) {
    Sample sample;
    sample.begin = std::move(begin);
    sample.end = std::move(end);
    sample.numIters = numIters;
    sample.subRegion = subRegion;
    m_times[region].push_back(sample);
  }

#ifdef USE_MPI  
  void printSummary(MPI_Comm comm);
#endif

  void writeSamples();
  
private:
  struct Sample {
    timespec begin;
    timespec end;
    unsigned numIters;
    unsigned subRegion;
  };
  
  std::vector<timespec> m_begin;
  std::vector<std::string> m_regions;
  std::vector<std::vector<Sample>> m_times;
  std::vector<bool> m_includeInSummary;
};
}

#endif // MONITORING_LOOPSTATISTICS_H_
