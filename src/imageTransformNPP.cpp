/* Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#define WINDOWS_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#pragma warning(disable : 4819)
#endif

#include <Exceptions.h>
#include <ImageIO.h>
#include <ImagesCPU.h>
#include <ImagesNPP.h>

#include <string.h>
#include <string>
#include <fstream>
#include <iostream>
#include <cmath>

#include <cuda_runtime.h>
#include <npp.h>

#include <helper_cuda.h>
#include <helper_string.h>

bool printfNPPinfo(int argc, char *argv[])
{
    const NppLibraryVersion *libVer = nppGetLibVersion();

    printf("NPP Library Version %d.%d.%d\n", libVer->major, libVer->minor,
           libVer->build);

    int driverVersion, runtimeVersion;
    cudaDriverGetVersion(&driverVersion);
    cudaRuntimeGetVersion(&runtimeVersion);

    printf("  CUDA Driver  Version: %d.%d\n", driverVersion / 1000,
           (driverVersion % 100) / 10);
    printf("  CUDA Runtime Version: %d.%d\n", runtimeVersion / 1000,
           (runtimeVersion % 100) / 10);

    // Min spec is SM 1.0 devices
    bool bVal = checkCudaCapabilities(1, 0);
    return bVal;
}

void printUsage()
{
    printf("Usage: imageTransformNPP [options]\n");
    printf("Options:\n");
    printf("  --input <path>       Input image file path\n");
    printf("  --output <path>      Output image file path (saved as .pgm format)\n");
    printf("  --rotation <angle>   Rotation angle in degrees (default: 45.0)\n");
    printf("  --scale <factor>     Scaling factor (default: 1.0)\n");
    printf("  --tx <value>         Translation in x-direction (default: 0.0)\n");
    printf("  --ty <value>         Translation in y-direction (default: 0.0)\n");
    printf("  --help               Show this help message\n");
    printf("\nNote: Arguments can be specified as --option=value or --option value\n");
}

int main(int argc, char *argv[])
{
    printf("%s Starting...\n\n", argv[0]);

    try
    {
        std::string sFilename;
        char *filePath;
        double rotationAngle = 45.0;  // Default rotation angle
        double scaleFactor = 1.0;     // Default scaling factor
        double translationX = 0.0;    // Default translation in x
        double translationY = 0.0;    // Default translation in y

        findCudaDevice(argc, (const char **)argv);

        if (printfNPPinfo(argc, argv) == false)
        {
            exit(EXIT_SUCCESS);
        }

        // Check for help flag
        if (checkCmdLineFlag(argc, (const char **)argv, "help"))
        {
            printUsage();
            exit(EXIT_SUCCESS);
        }

        // Parse command line arguments
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            
            // Handle --rotation argument (both --rotation=value and --rotation value formats)
            if (arg.find("--rotation=") == 0) {
                rotationAngle = atof(arg.substr(11).c_str());
            }
            else if (arg == "--rotation" && i + 1 < argc) {
                rotationAngle = atof(argv[i + 1]);
                i++; // Skip the next argument since we consumed it
            }
            // Handle --scale argument (both --scale=value and --scale value formats)
            else if (arg.find("--scale=") == 0) {
                scaleFactor = atof(arg.substr(8).c_str());
            }
            else if (arg == "--scale" && i + 1 < argc) {
                scaleFactor = atof(argv[i + 1]);
                i++; // Skip the next argument since we consumed it
            }
            // Handle --tx argument
            else if (arg.find("--tx=") == 0) {
                translationX = atof(arg.substr(5).c_str());
            }
            else if (arg == "--tx" && i + 1 < argc) {
                translationX = atof(argv[i + 1]);
                i++;
            }
            // Handle --ty argument
            else if (arg.find("--ty=") == 0) {
                translationY = atof(arg.substr(5).c_str());
            }
            else if (arg == "--ty" && i + 1 < argc) {
                translationY = atof(argv[i + 1]);
                i++;
            }
        }

        printf("SE(2) x S Transformation Parameters:\n");
        printf("  Rotation angle: %.2f degrees\n", rotationAngle);
        printf("  Scale factor: %.2f\n", scaleFactor);
        printf("  Translation: (%.2f, %.2f)\n", translationX, translationY);

        if (checkCmdLineFlag(argc, (const char **)argv, "input"))
        {
            getCmdLineArgumentString(argc, (const char **)argv, "input", &filePath);
        }
        else
        {
            filePath = sdkFindFilePath("Lena_gray.png", argv[0]);
        }

        if (filePath)
        {
            sFilename = filePath;
        }
        else
        {
            sFilename = "data/Lena_gray.png";
        }

        // if we specify the filename at the command line, then we only test
        // sFilename[0].
        int file_errors = 0;
        std::ifstream infile(sFilename.data(), std::ifstream::in);

        if (infile.good())
        {
            std::cout << "SO(2) x S Transform opened: <" << sFilename.data()
                      << "> successfully!" << std::endl;
            file_errors = 0;
            infile.close();
        }
        else
        {
            std::cout << "SO(2) x S Transform unable to open: <" << sFilename.data() << ">"
                      << std::endl;
            file_errors++;
            infile.close();
        }

        if (file_errors > 0)
        {
            exit(EXIT_FAILURE);
        }

        std::string sResultFilename = sFilename;

        std::string::size_type dot = sResultFilename.rfind('.');

        if (dot != std::string::npos)
        {
            sResultFilename = sResultFilename.substr(0, dot);
        }

        if (checkCmdLineFlag(argc, (const char **)argv, "output"))
        {
            char *outputFilePath;
            // First try the standard getCmdLineArgumentString for --output=filename format
            if (getCmdLineArgumentString(argc, (const char **)argv, "output", &outputFilePath)) {
                sResultFilename = outputFilePath;
            } else {
                // Handle --output filename format (separate arguments)
                for (int i = 1; i < argc - 1; i++) {
                    if (strcmp(argv[i], "--output") == 0) {
                        sResultFilename = argv[i + 1];
                        break;
                    }
                }
            }
            
            // Validate that output filename ends with .pgm
            if (sResultFilename.length() < 4 || sResultFilename.substr(sResultFilename.length() - 4) != ".pgm") {
                std::cerr << "Error: Output filename must have .pgm extension. Got: " << sResultFilename << std::endl;
                std::cerr << "Example: --output result.pgm" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            // No output specified, add .pgm extension to default filename
            sResultFilename += ".pgm";
        }

        // declare a host image object for an 8-bit grayscale image
        npp::ImageCPU_8u_C1 oHostSrc;
        // load grayscale image from disk
        npp::loadImage(sFilename, oHostSrc);
        // declare a device image and copy construct from the host image,
        // i.e. upload host to device
        npp::ImageNPP_8u_C1 oDeviceSrc(oHostSrc);

        // create struct with the ROI size
        NppiSize oSrcSize = {(int)oDeviceSrc.width(), (int)oDeviceSrc.height()};
        NppiRect oSrcROI = {0, 0, (int)oDeviceSrc.width(), (int)oDeviceSrc.height()};

        // Convert rotation angle to radians
        double angleRad = rotationAngle * M_PI / 180.0;

        // Calculate the bounding box for the combined transformation
        // We need to consider both rotation and scaling
        double cosAngle = cos(angleRad);
        double sinAngle = sin(angleRad);

        // Calculate the corners of the transformed image
        double corners[4][2] = {
            {0.0, 0.0},
            {(double)oSrcSize.width, 0.0},
            {(double)oSrcSize.width, (double)oSrcSize.height},
            {0.0, (double)oSrcSize.height}
        };

        double minX = 0, maxX = 0, minY = 0, maxY = 0;
        for (int i = 0; i < 4; i++)
        {
            double x = corners[i][0] - oSrcSize.width / 2.0;
            double y = corners[i][1] - oSrcSize.height / 2.0;
            
            // Apply rotation and scaling
            double rotScaledX = scaleFactor * (x * cosAngle - y * sinAngle);
            double rotScaledY = scaleFactor * (x * sinAngle + y * cosAngle);

            // The final transformed point is centered in the destination image frame,
            // then translated by the user-provided values.
            double newX = rotScaledX + oSrcSize.width / 2.0 + translationX;
            double newY = rotScaledY + oSrcSize.height / 2.0 + translationY;
            
            if (i == 0)
            {
                minX = maxX = newX;
                minY = maxY = newY;
            }
            else
            {
                minX = std::min(minX, newX);
                maxX = std::max(maxX, newX);
                minY = std::min(minY, newY);
                maxY = std::max(maxY, newY);
            }
        }

        int dstWidth = (int)ceil(maxX - minX);
        int dstHeight = (int)ceil(maxY - minY);

        // allocate device image for the transformed image
        npp::ImageNPP_8u_C1 oDeviceDst(dstWidth, dstHeight);
        
        // Set up the affine transformation matrix for SE(2) x S
        // Matrix format: [a11 a12 a13; a21 a22 a23]
        // For rotation + scaling + translation: [s*cos(θ) -s*sin(θ) tx; s*sin(θ) s*cos(θ) ty]
        double aCoeffs[2][3];
        
        // Calculate translation to center the result and apply user translation
        double tx = dstWidth / 2.0 - scaleFactor * (oSrcSize.width / 2.0 * cosAngle - oSrcSize.height / 2.0 * sinAngle) + translationX;
        double ty = dstHeight / 2.0 - scaleFactor * (oSrcSize.width / 2.0 * sinAngle + oSrcSize.height / 2.0 * cosAngle) + translationY;
        
        aCoeffs[0][0] = scaleFactor * cosAngle;   // a11
        aCoeffs[0][1] = -scaleFactor * sinAngle;  // a12
        aCoeffs[0][2] = tx;                       // a13
        aCoeffs[1][0] = scaleFactor * sinAngle;   // a21
        aCoeffs[1][1] = scaleFactor * cosAngle;   // a22
        aCoeffs[1][2] = ty;                       // a23

        NppiSize oDstSize = {dstWidth, dstHeight};
        NppiRect oDstROI = {0, 0, dstWidth, dstHeight};

        // Perform the combined SO(2) x S transformation using affine warp
        NPP_CHECK_NPP(nppiWarpAffine_8u_C1R(
            oDeviceSrc.data(), oSrcSize, oDeviceSrc.pitch(), oSrcROI,
            oDeviceDst.data(), oDeviceDst.pitch(), oDstROI,
            aCoeffs, NPPI_INTER_LINEAR));

        // declare a host image for the result
        npp::ImageCPU_8u_C1 oHostDst(oDeviceDst.size());
        // and copy the device result data into it
        oDeviceDst.copyTo(oHostDst.data(), oHostDst.pitch());

        saveImage(sResultFilename, oHostDst);
        std::cout << "Saved transformed image in PGM format: " << sResultFilename << std::endl;
        std::cout << "Applied transformations: Rotation=" << rotationAngle 
                  << "°, Scale=" << scaleFactor << ", Translation=(" << translationX 
                  << ", " << translationY << ")" << std::endl;

        nppiFree(oDeviceSrc.data());
        nppiFree(oDeviceDst.data());

        exit(EXIT_SUCCESS);
    }
    catch (npp::Exception &rException)
    {
        std::cerr << "Program error! The following exception occurred: \n";
        std::cerr << rException << std::endl;
        std::cerr << "Aborting." << std::endl;

        exit(EXIT_FAILURE);
    }
    catch (...)
    {
        std::cerr << "Program error! An unknown type of exception occurred. \n";
        std::cerr << "Aborting." << std::endl;

        exit(EXIT_FAILURE);
        return -1;
    }

    return 0;
}
