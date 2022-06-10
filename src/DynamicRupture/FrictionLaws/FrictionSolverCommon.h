#ifndef SEISSOL_FRICTIONSOLVER_COMMON_H
#define SEISSOL_FRICTIONSOLVER_COMMON_H

#include "DynamicRupture/Misc.h"
#include "DynamicRupture/Parameters.h"
#include "Initializer/DynamicRupture.h"
#include "Kernels/DynamicRupture.h"

namespace seissol::dr::friction_law {
struct Common {
  /**
   * Contains common functions required both for CPU and GPU impl.
   * of Dynamic Rupture solvers. The functions placed in
   * this class definition (of the header file) result
   * in the function inlining required for GPU impl.
   */

  public:
  /**
   * Calculate traction and normal stress at the interface of a face.
   * Using equations (A2) from Pelties et al. 2014
   * This is equation (4.53) of Carsten Uphoff's thesis,
   * this function returns the Theta_i from eq (4.53).
   * Definiton of eta and impedance Z are found in dissertation of Carsten Uphoff
   *
   * @param[out] faultStresses contains normalStress, traction1, traction2
   *             at the 2d face quadrature nodes evaluated at the time
   *             quadrature points
   * @param[in] impedanceMatrices contains eta and impedance values
   * @param[in] qInterpolatedPlus a plus side dofs interpolated at time sub-intervals
   * @param[in] qInterpolatedMinus a minus side dofs interpolated at time sub-intervals
   */
  static void precomputeStressFromQInterpolated(
      FaultStresses& faultStresses,
      const ImpedanceMatrices& impedanceMatrices,
      real qInterpolatedPlus[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
      real qInterpolatedMinus[CONVERGENCE_ORDER][tensor::QInterpolated::size()]) {

    static_assert(tensor::QInterpolated::Shape[0] == tensor::resample::Shape[0],
                  "Different number of quadrature points?");

    seissol::dynamicRupture::kernel::computeTheta krnl;
    krnl.extractVelocities = init::extractVelocities::Values;
    krnl.extractTractions = init::extractTractions::Values;

    // Compute Theta from eq (4.53) in Carsten's thesis
    krnl.Zplus = impedanceMatrices.impedanceNeig.data();
    krnl.Zminus = impedanceMatrices.impedance.data();
    krnl.eta = impedanceMatrices.eta.data();

    real thetaBuffer[tensor::theta::size()] = {};
    krnl.theta = thetaBuffer;
    auto thetaView = init::theta::view::create(thetaBuffer);

    // TODO: Integrate loop over o into the kernel
    for (unsigned o = 0; o < CONVERGENCE_ORDER; ++o) {
      krnl.Qplus = qInterpolatedPlus[o];
      krnl.Qminus = qInterpolatedMinus[o];
      krnl.execute();

#ifdef ACL_DEVICE_OFFLOAD
#pragma omp loop bind(parallel)
#endif // ACL_DEVICE_OFFLOAD
      for (unsigned i = 0; i < misc::numPaddedPoints; ++i) {
        faultStresses.normalStress[o][i] = thetaView(i, 0);
        faultStresses.traction1[o][i] = thetaView(i, 1);
        faultStresses.traction2[o][i] = thetaView(i, 2);
#ifdef USE_POROELASTIC
        faultStresses.fluidPressure[o][i] = thetaView(i, 3);
#endif
      }
    }
  }

  /**
   * Integrate over all Time points with the time weights and calculate the traction for each side
   * according to Carsten Uphoff Thesis: EQ.: 4.60
   *
   * @param[in] faultStresses
   * @param[in] tractionResults
   * @param[in] impedancenceMatrices
   * @param[in] qInterpolatedPlus
   * @param[in] qInterpolatedMinus
   * @param[in] timeWeights
   * @param[out] imposedStatePlus
   * @param[out] imposedStateMinus
   */
  static void postcomputeImposedStateFromNewStress(
      const FaultStresses& faultStresses,
      const TractionResults& tractionResults,
      const ImpedanceMatrices& impedanceMatrices,
      real imposedStatePlus[tensor::QInterpolated::size()],
      real imposedStateMinus[tensor::QInterpolated::size()],
      real qInterpolatedPlus[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
      real qInterpolatedMinus[CONVERGENCE_ORDER][tensor::QInterpolated::size()],
      double timeWeights[CONVERGENCE_ORDER]) {
    // set imposed state to zero
#ifdef ACL_DEVICE_OFFLOAD
#pragma omp loop bind(parallel)
#endif // ACL_DEVICE_OFFLOAD
    for (unsigned int i = 0; i < tensor::QInterpolated::size(); i++) {
      imposedStatePlus[i] = static_cast<real>(0.0);
      imposedStateMinus[i] = static_cast<real>(0.0);
    }

    // setup kernel
    seissol::dynamicRupture::kernel::computeImposedStateM krnlM;
    krnlM.extractVelocities = init::extractVelocities::Values;
    krnlM.extractTractions = init::extractTractions::Values;
    krnlM.mapToVelocities = init::mapToVelocities::Values;
    krnlM.mapToTractions = init::mapToTractions::Values;
    krnlM.Zminus = impedanceMatrices.impedance.data();
    krnlM.imposedState = imposedStateMinus;

    seissol::dynamicRupture::kernel::computeImposedStateP krnlP;
    krnlP.extractVelocities = init::extractVelocities::Values;
    krnlP.extractTractions = init::extractTractions::Values;
    krnlP.mapToVelocities = init::mapToVelocities::Values;
    krnlP.mapToTractions = init::mapToTractions::Values;
    krnlP.Zplus = impedanceMatrices.impedanceNeig.data();
    krnlP.imposedState = imposedStatePlus;

    real thetaBuffer[tensor::theta::size()] = {};
    auto thetaView = init::theta::view::create(thetaBuffer);
    krnlM.theta = thetaBuffer;
    krnlP.theta = thetaBuffer;

    for (unsigned o = 0; o < CONVERGENCE_ORDER; ++o) {
      auto weight = timeWeights[o];
      // copy values to yateto dataformat
      for (unsigned i = 0; i < misc::numPaddedPoints; ++i) {
        thetaView(i, 0) = faultStresses.normalStress[o][i];
        thetaView(i, 1) = tractionResults.traction1[o][i];
        thetaView(i, 2) = tractionResults.traction2[o][i];
#ifdef USE_POROELASTIC
        thetaView(i, 3) = faultStresses.fluidPressure[o][i];
#endif
      }
      // execute kernel (and hence update imposedStatePlus/Minus)
      krnlM.Qminus = qInterpolatedMinus[o];
      krnlM.weight = weight;
      krnlM.execute();

      krnlP.Qplus = qInterpolatedPlus[o];
      krnlP.weight = weight;
      krnlP.execute();
    }
  }

  /**
   * output rupture front, saves update time of the rupture front
   * rupture front is the first registered change in slip rates that exceeds 0.001
   *
   * param[in,out] ruptureTimePending
   * param[out] ruptureTime
   * param[in] slipRateMagnitude
   * param[in] fullUpdateTime
   */
  static void saveRuptureFrontOutput(bool ruptureTimePending[misc::numPaddedPoints],
                                     real ruptureTime[misc::numPaddedPoints],
                                     real slipRateMagnitude[misc::numPaddedPoints],
                                     real fullUpdateTime) {
#ifdef ACL_DEVICE_OFFLOAD
#pragma omp loop bind(parallel)
#endif // ACL_DEVICE_OFFLOAD
    for (unsigned pointIndex = 0; pointIndex < misc::numPaddedPoints; pointIndex++) {
      constexpr real ruptureFrontThreshold = 0.001;
      if (ruptureTimePending[pointIndex] && slipRateMagnitude[pointIndex] > ruptureFrontThreshold) {
        ruptureTime[pointIndex] = fullUpdateTime;
        ruptureTimePending[pointIndex] = false;
      }
    }
  }

  /**
   * Save the maximal computed slip rate magnitude in peakSlipRate
   *
   * param[in] slipRateMagnitude
   * param[in, out] peakSlipRate
   */
  static void savePeakSlipRateOutput(real slipRateMagnitude[misc::numPaddedPoints],
                                     real peakSlipRate[misc::numPaddedPoints]) {

#ifdef ACL_DEVICE_OFFLOAD
#pragma omp loop bind(parallel)
#endif // ACL_DEVICE_OFFLOAD
    for (unsigned pointIndex = 0; pointIndex < misc::numPaddedPoints; pointIndex++) {
      peakSlipRate[pointIndex] = std::max(peakSlipRate[pointIndex], slipRateMagnitude[pointIndex]);
    }
  }

  /**
   * Compute and store element-averaged slip to determine the magnitude of an earthquake.
   * In calc_seissol.f90 this value will be multiplied by the element surface
   * and the seismic moment is outputted once at the end of the simulation.
   *
   * @param[in] tmpSlip
   * @param[out] averagedSlip
   */
  static void saveAverageSlipOutput(real tmpSlip[misc::numPaddedPoints], real& averagedSlip) {
    real sumOfTmpSlip = 0;
    for (unsigned pointIndex = 0; pointIndex < misc::numberOfBoundaryGaussPoints; pointIndex++) {
      sumOfTmpSlip += tmpSlip[pointIndex];
    }
    averagedSlip += sumOfTmpSlip / misc::numberOfBoundaryGaussPoints;
  }
};
} // namespace seissol::dr::friction_law

#endif // SEISSOL_FRICTIONSOLVER_COMMON_H
