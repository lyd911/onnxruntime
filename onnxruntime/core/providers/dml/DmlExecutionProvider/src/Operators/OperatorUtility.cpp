// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "precomp.h"
#include "OperatorUtility.h"
#include "core/providers/dml/OperatorAuthorHelper/OperatorRegistration.h"

namespace Dml
{
    namespace ActivationHelper
    {
        float GetDefaultAlpha(DML_OPERATOR_TYPE function)
        {
            switch (function)
            {
            case DML_OPERATOR_ACTIVATION_ELU:
                return 1.0f;

            case DML_OPERATOR_ACTIVATION_LEAKY_RELU:
                return .01f;

            case DML_OPERATOR_ACTIVATION_PARAMETRIC_SOFTPLUS:
                // This function's default alpha value is not specified by ONNX, but 1.0 is logical
                return 1.0f;

            case DML_OPERATOR_ACTIVATION_SCALED_ELU:
                return 1.67326319217681884765625f;

            case DML_OPERATOR_ACTIVATION_THRESHOLDED_RELU:
                return 1.0f;

            case DML_OPERATOR_ACTIVATION_HARD_SIGMOID:
                return 0.2f;

            case DML_OPERATOR_ACTIVATION_SCALED_TANH:
                // This function's default alpha value is not specified by ONNX, but 1.0 is logical
                return 1.0f;

            case DML_OPERATOR_ACTIVATION_TANH:
            case DML_OPERATOR_ACTIVATION_SOFTSIGN:
            case DML_OPERATOR_ACTIVATION_SOFTPLUS:
            case DML_OPERATOR_ACTIVATION_SOFTMAX:
            case DML_OPERATOR_ACTIVATION_SIGMOID:
            case DML_OPERATOR_ACTIVATION_RELU:
            case DML_OPERATOR_ACTIVATION_PARAMETERIZED_RELU:
            case DML_OPERATOR_ACTIVATION_LOG_SOFTMAX:
            case DML_OPERATOR_ACTIVATION_HARDMAX:
            case DML_OPERATOR_ACTIVATION_IDENTITY:
            default:
                assert(false);
                return 1.0f;
            }
        }

        float GetDefaultBeta(DML_OPERATOR_TYPE function)
        {
            switch (function)
            {
            case DML_OPERATOR_ACTIVATION_HARD_SIGMOID:
                return 0.5f;

            case DML_OPERATOR_ACTIVATION_PARAMETRIC_SOFTPLUS:
                // This function's default beta value is not specified by ONNX, but 1.0 is logical
                return 1.0f;

            case DML_OPERATOR_ACTIVATION_SCALED_TANH:
                // This function's default beta value is not specified by ONNX, but 1.0 is logical
                return 1.0f;

            case DML_OPERATOR_ACTIVATION_SOFTSIGN:
            case DML_OPERATOR_ACTIVATION_SOFTPLUS:
            case DML_OPERATOR_ACTIVATION_SOFTMAX:
            case DML_OPERATOR_ACTIVATION_SIGMOID:
            case DML_OPERATOR_ACTIVATION_TANH:
            case DML_OPERATOR_ACTIVATION_RELU:
            case DML_OPERATOR_ACTIVATION_PARAMETERIZED_RELU:
            case DML_OPERATOR_ACTIVATION_LOG_SOFTMAX:
            case DML_OPERATOR_ACTIVATION_HARDMAX:
            case DML_OPERATOR_ACTIVATION_THRESHOLDED_RELU:
            default:
                assert(false);
                return 0.0f;
            }
        }

        float GetDefaultGamma(DML_OPERATOR_TYPE function)
        {
            switch (function)
            {
            case DML_OPERATOR_ACTIVATION_SCALED_ELU:
                return 1.05070102214813232421875f;

            default:
                assert(false);
                return 0.0f;
            }
        }

        float GetDefaultLambda(DML_OPERATOR_TYPE function)
        {
            switch (function)
            {
            case DML_OPERATOR_ACTIVATION_SHRINK:
                return 0.5f;

            default:
                assert(false);
                return 0.0f;
            }
        }

        float GetDefaultBias(DML_OPERATOR_TYPE function)
        {
            switch (function)
            {
            case DML_OPERATOR_ACTIVATION_SHRINK:
                return 0.0f;

            default:
                assert(false);
                return 0.0f;
            }
        }
    } // namespace ActivationHelper
    
    namespace FusionHelpers
    {
        struct OperatorInfo
        {
            std::string_view type;
            std::string_view domain;
            int sinceVersion;
            std::vector<std::string_view> activationFilter;
            std::optional<uint32_t> inputCountFilter;
        };

        static bool operator==(const OperatorInfo& lhs, const OperatorInfo& rhs)
        {
            return (lhs.type == rhs.type && lhs.domain == rhs.domain && lhs.sinceVersion == rhs.sinceVersion);
        }

        static const OperatorInfo c_fusableOps[] =
        {
            OperatorInfo{ "Conv",                      onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Conv },
            OperatorInfo{ "ConvTranspose",             onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_ConvTranspose },
            OperatorInfo{ "BatchNormalization",        onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_BatchNormalization },
            OperatorInfo{ "InstanceNormalization",     onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_InstanceNormalization },
            OperatorInfo{ "MeanVarianceNormalization", onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_MeanVarianceNormalization },
            OperatorInfo{ "Gemm",                      onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Gemm },
            OperatorInfo{ "MatMul",                    onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_MatMul },

            // The filter for activation functions maps to what DML's fused op internally fuses at the shader level.
            OperatorInfo{ "Add",                       onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Add, {"Relu", "LeakyRelu"} },
            OperatorInfo{ "Sum",                       onnxruntime::kOnnxDomain, OnnxOperatorSet8::sc_sinceVer_Sum, {"Relu", "LeakyRelu"}, 2 },
        };

        // Not all activations can be fused - only simple elementwise activations (i.e. activation functions which
        // don't require a reduction pass) can be fused.
        static const OperatorInfo c_activationOps[] =
        {
            OperatorInfo{ "Sigmoid",            onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Sigmoid },
            OperatorInfo{ "HardSigmoid",        onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_HardSigmoid },
            OperatorInfo{ "Tanh",               onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Tanh },
            OperatorInfo{ "ScaledTanh",         onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_ScaledTanh },
            OperatorInfo{ "Relu",               onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Relu },
            OperatorInfo{ "LeakyRelu",          onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_LeakyRelu },
            OperatorInfo{ "PRelu",              onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_PRelu },
            OperatorInfo{ "ThresholdedRelu",    onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_ThresholdedRelu },
            OperatorInfo{ "Elu",                onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Elu },
            OperatorInfo{ "Selu",               onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Selu },
            OperatorInfo{ "Softsign",           onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Softsign },
            OperatorInfo{ "Softplus",           onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Softplus },
            OperatorInfo{ "ParametricSoftplus", onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_ParametricSoftplus },
            OperatorInfo{ "Dropout",            onnxruntime::kOnnxDomain, OnnxOperatorSet7::sc_sinceVer_Dropout },
        };

        std::optional<FusedOpProperties> TryGetFusedOp(
            std::string_view candidateOpType,
            std::string_view candidateOpDomain,
            int candidateOpSinceVersion,
            int candidateOpInputCount,
            std::string_view activationOpType,
            std::string_view activationOpDomain,
            int activationOpSinceVersion)
        {
            auto opIt = std::find(
                std::begin(c_fusableOps),
                std::end(c_fusableOps),
                OperatorInfo{ candidateOpType, candidateOpDomain, candidateOpSinceVersion }
                );
            if (opIt == std::end(c_fusableOps))
            {
                return std::nullopt;
            }

            auto it = std::find(
                std::begin(c_activationOps),
                std::end(c_activationOps),
                OperatorInfo{ activationOpType, activationOpDomain, activationOpSinceVersion }
                );
            if (it == std::end(c_activationOps))
            {
                return std::nullopt;
            }

            if (!opIt->activationFilter.empty() && 
                std::find(opIt->activationFilter.begin(), opIt->activationFilter.end(), activationOpType) ==  opIt->activationFilter.end())
            {
                return std::nullopt;
            }

            if (opIt->inputCountFilter && *opIt->inputCountFilter != candidateOpInputCount)
            {
                return std::nullopt;
            }

            // All fused ops just have "Fused" prepended to their name
            std::string fusedOpType = std::string("Fused").append(candidateOpType);

            return FusedOpProperties{ std::move(fusedOpType), onnxruntime::kMSDmlDomain };
        }

        bool IsFusableActivationOperator(std::string_view opType, std::string_view domain, int sinceVersion)
        {
            auto it = std::find(std::begin(c_activationOps), std::end(c_activationOps), OperatorInfo{ opType, domain, sinceVersion });
            return it != std::end(c_activationOps);
        }

        std::optional<ActivationOperatorDesc> TryGetFusedActivationDesc(const MLOperatorKernelCreationContext& kernelInfo)
        {
            if (!kernelInfo.HasAttribute(AttrName::FusedActivation, MLOperatorAttributeType::String))
            {
                return std::nullopt; // No activation
            }

            ActivationOperatorDesc activation = {};

            auto activationName = kernelInfo.GetAttribute(AttrName::FusedActivation);
            auto activationDomain = kernelInfo.GetAttribute(AttrName::FusedActivationDomain);
            auto activationVersion = static_cast<int>(kernelInfo.GetAttribute<int64_t>(AttrName::FusedActivationSinceVersion));
            ML_CHECK_VALID_ARGUMENT(FusionHelpers::IsFusableActivationOperator(activationName, activationDomain, activationVersion));

            if (activationName == "Linear")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_LINEAR;
                activation.params.linear.Alpha = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedAlpha, ActivationHelper::GetDefaultAlpha(activation.activationType));
                activation.params.linear.Beta = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedBeta, ActivationHelper::GetDefaultBeta(activation.activationType));
            }
            else if (activationName == "Sigmoid")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_SIGMOID;
            }
            else if (activationName == "HardSigmoid")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_HARD_SIGMOID;
                activation.params.hardSigmoid.Alpha = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedAlpha, ActivationHelper::GetDefaultAlpha(activation.activationType));
                activation.params.hardSigmoid.Beta = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedBeta, ActivationHelper::GetDefaultBeta(activation.activationType));
            }
            else if (activationName == "Tanh")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_TANH;
            }
            else if (activationName == "ScaledTanh")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_SCALED_TANH;
                activation.params.scaledTanh.Alpha = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedAlpha, ActivationHelper::GetDefaultAlpha(activation.activationType));
                activation.params.scaledTanh.Beta = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedBeta, ActivationHelper::GetDefaultBeta(activation.activationType));
            }
            else if (activationName == "Relu")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_RELU;
            }
            else if (activationName == "LeakyRelu")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_LEAKY_RELU;
                activation.params.leakyRelu.Alpha = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedAlpha, ActivationHelper::GetDefaultAlpha(activation.activationType));
            }
            else if (activationName == "ThresholdedRelu")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_THRESHOLDED_RELU;
                activation.params.thresholdedRelu.Alpha = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedAlpha, ActivationHelper::GetDefaultAlpha(activation.activationType));
            }
            else if (activationName == "Elu")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_ELU;
                activation.params.elu.Alpha = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedAlpha, ActivationHelper::GetDefaultAlpha(activation.activationType));
            }
            else if (activationName == "Selu")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_SCALED_ELU;
                activation.params.scaledElu.Alpha = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedAlpha, ActivationHelper::GetDefaultAlpha(activation.activationType));
                activation.params.scaledElu.Gamma = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedGamma, ActivationHelper::GetDefaultGamma(activation.activationType));
            }
            else if (activationName == "Softsign")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_SOFTSIGN;
            }
            else if (activationName == "Softplus")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_SOFTPLUS;
                activation.params.softplus.Steepness = 1.0f;
            }
            else if (activationName == "ParametricSoftplus")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_PARAMETRIC_SOFTPLUS;
                activation.params.parametricSoftplus.Alpha = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedAlpha, ActivationHelper::GetDefaultAlpha(activation.activationType));
                activation.params.parametricSoftplus.Beta = kernelInfo.GetOptionalAttribute<float>(AttrName::FusedBeta, ActivationHelper::GetDefaultBeta(activation.activationType));
            }
            else if (activationName == "Shrink")
            {
                activation.activationType = DML_OPERATOR_ACTIVATION_SHRINK;
                activation.params.shrink.Bias = kernelInfo.GetOptionalAttribute<float>(AttrName::Bias, ActivationHelper::GetDefaultBias(activation.activationType));
                activation.params.shrink.Threshold = kernelInfo.GetOptionalAttribute<float>(AttrName::Lambda, ActivationHelper::GetDefaultLambda(activation.activationType));
            }
            else if (activationName == "Dropout")
            {
                return std::nullopt;
            }
            else
            {
                ML_INVALID_ARGUMENT("Unsupported activation function.");
            }

            return activation;
        }

        /*static*/ std::string GetFusedAttributeName(std::string_view name)
        {
            return std::string("fused_").append(name);
        }

    } // namespace FusionHelpers

    uint32_t GetDmlAdjustedAxis(int32_t onnxAxis, const MLOperatorKernelCreationContext& kernelCreationContext, uint32_t dmlDimCount)
    {
        const std::vector<DimensionType> inputDimensions = kernelCreationContext.GetTensorShapeDescription().GetInputTensorShape(0);
        uint32_t onnxDimCount = gsl::narrow_cast<uint32_t>(inputDimensions.size());
        onnxAxis = HandleNegativeAxis(onnxAxis, onnxDimCount);
        return GetDmlAdjustedAxis(onnxAxis, onnxDimCount, dmlDimCount);
    }

    // Adjust the axis value to compensate for padding any upper dimensions (unsqueezing).
    uint32_t GetDmlAdjustedAxis(int32_t onnxAxis, uint32_t onnxDimCount, uint32_t dmlDimCount)
    {
        ML_CHECK_VALID_ARGUMENT(dmlDimCount >= onnxDimCount);
        onnxAxis = HandleNegativeAxis(onnxAxis, onnxDimCount);
        uint32_t dmlAxis = onnxAxis + dmlDimCount - onnxDimCount;
        return dmlAxis;
    }

    DML_INTERPOLATION_MODE MapStringToInteropolationMode(std::string_view mode)
    {
        // The ONNX modes are "nearest" and "linear."  Other modes exist for compatibility,
        // since Winml supported them in the past.
        if (mode == "NEAREST" || mode == "nearest" || mode == "nn" || mode == "NN") {
            return DML_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
        }
        else if (mode == "BILINEAR" || mode == "bilinear" || mode == "linear")
        {
            return DML_INTERPOLATION_MODE_LINEAR;
        }
        else
        {
            ML_INVALID_ARGUMENT("Unknown sampling interpolation mode.");
        }
    }

} // namespace Dml
