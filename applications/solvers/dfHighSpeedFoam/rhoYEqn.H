start = std::clock();

if (!inviscid)
{
    hDiffCorrFlux = Zero;
    diffAlphaD = Zero;
    sumYDiffError = Zero;

    forAll(Y, i)
    {
        sumYDiffError += chemistry->rhoD(i)*fvc::grad(Y[i]);
    }
}

tmp<fv::convectionScheme<scalar>> mvConvection
(
    fv::convectionScheme<scalar>::New
    (
        mesh,
        fields,
        rhoPhi,
        mesh.divScheme("div(phi,Yi_h)")
    )
);

end = std::clock();
time_monitor_corrDiff += double(end - start) / double(CLOCKS_PER_SEC);

{
    start = std::clock();

    if((ddtSchemes == "RK2SSP") || (ddtSchemes == "RK3SSP"))
    {
        if(chemScheme == "direct")
        {
            chemistry->calculateW();
        }
    }
    else
    {
        combustion->correct();
    }

    label flag_mpi_init;
    MPI_Initialized(&flag_mpi_init);
    if(flag_mpi_init) MPI_Barrier(PstreamGlobals::MPI_COMM_FOAM);
    end = std::clock();
    time_monitor_chem += double(end - start) / double(CLOCKS_PER_SEC);

    volScalarField Yt(0.0*Y[0]);

    start = std::clock();
    forAll(Y, i)
    {
        volScalarField& Yi = Y[i];
        // surfaceScalarField rhoPhiYi(rhoPhi*fvc::interpolate(Yi, "Yi").ref());

        if (!inviscid)
        {
            hDiffCorrFlux += chemistry->hei(i)*(chemistry->rhoD(i)*fvc::grad(Yi) - Yi*sumYDiffError);
            diffAlphaD += fvc::laplacian(thermo.alpha()*chemistry->hei(i), Yi);
        }

        // if (i != inertIndex)
        {
            if((ddtSchemes == "RK2SSP") || (ddtSchemes == "RK3SSP"))
            {
                rhoYi_rhs = -fvc::div(rhoPhiYi[i]);

                if(chemScheme == "direct")
                {
                    rhoYi_rhs.ref() += chemistry->wrate(i);
                    Info <<"max reaction rate "<< Yi.name() << " is " << max(chemistry->wrate(i)).value() << endl;
                }


                rhoYi[i] =   rkcoe1[nrk]*rhoYi_save[i]
                         + rkcoe2[nrk]*rhoYi[i]
                         + rkcoe3[nrk]*rhoYi_rhs*runTime.deltaT();

                Yi=rhoYi[i]/rho;
                Yi.max(0.0);

                if (!inviscid)
                {
                    const surfaceScalarField phiUc = linearInterpolate(sumYDiffError) & mesh.Sf();
                    tmp<volScalarField> DEff = chemistry->rhoD(i) + turbulence->mut()/Sct;

                    rhoYi_rhs =
                        (
                            turbName == "laminar"
                            ?  (fvc::laplacian(DEff(), Yi) - fvc::div(phiUc,Yi,"div(phi,Yi_h)"))
                            :  fvc::laplacian(DEff(), Yi)
                        );

                    rhoYi[i] += rkcoe3[nrk]*rhoYi_rhs*runTime.deltaT();

                    Yi = rhoYi[i]/rho;
                    Yi.max(0.0);

                }

                Yi.correctBoundaryConditions();
                rhoYi[i] = rho*Yi;

            }
            else
            {
                // original convection term
                // fvScalarMatrix YiEqn
                // (
                //      fvm::ddt(rho, Yi)
                //    + mvConvection->fvmDiv(rhoPhi, Yi)
                //   ==
                //       combustion->R(Yi)
                //     + parcels.SYi(i, Yi)
                // );

                solve
                (
                      fvm::ddt(rhoYi[i])
                    + fvc::div(rhoPhiYi[i])
                ==
                      chemistry->RR(i)
                    + parcels.SYi(i, rhoYi[i])
                );

                Yi=rhoYi[i]/rho;
                Yi.max(0.0);

                if (!inviscid)
                {
                    const surfaceScalarField phiUc = linearInterpolate(sumYDiffError) & mesh.Sf();
                    tmp<volScalarField> DEff = chemistry->rhoD(i) + turbulence->mut()/Sct;

                    // original term
                    // YiEqn -=
                    //     (
                    //         turbName == "laminar"
                    //         ? (fvm::laplacian(DEff(), Yi) - mvConvection->fvmDiv(phiUc, Yi))
                    //         : fvm::laplacian(DEff(), Yi)
                    //     );


                    fvScalarMatrix YiEqn
                    (
                            fvm::ddt(rho, Yi) - fvc::ddt(rho, Yi)
                         -
                            (
                                turbName == "laminar"
                                ?  (fvm::laplacian(DEff(), Yi) - mvConvection->fvmDiv(phiUc, Yi))
                                :  fvm::laplacian(DEff(), Yi)
                            )
                    );

                    YiEqn.relax();

                    YiEqn.solve("Yi");

                    Yi.max(0.0);
                }
                // original term
                // YiEqn.relax();
                // YiEqn.solve("Yi");
                // Yi.max(0.0);
                rhoYi[i] = rho*Yi;

            }
            Yt += Yi;
        }
    }

    // Y[inertIndex] = scalar(1) - Yt;
    // Y[inertIndex].max(0.0);
    // rhoYi[inertIndex] = rho*Y[inertIndex];

    forAll(Y, i)
    {
        Y[i] = Y[i]/Yt;
        Y[i].max(0.0);
        rhoYi[i] = rho*Y[i];
    }

    end = std::clock();
    time_monitor_Y += double(end - start) / double(CLOCKS_PER_SEC);
}
