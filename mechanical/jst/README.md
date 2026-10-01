# JST PH connectors (S1-S3 probe plugs)

The probe wires' connectors: S2B-PH-K-S on the board (S1-S3, LCSC C157932), mating PHR-2 housing
(C157955) with SPH-002T-P0.5S crimps (C111515).

JST's CAD data (https://www.jst-mfg.com/product/detail_e.php?series=199) comes after its license
form; the license forbids copying or disclosing it to third parties, so it is kept outside the
project, in ~/k230d-hw/JST_PH (PHR-2.STEP, S2B-PH-K-S.STEP, the SPH-002T-P0.5S drawing KRD-05353-4,
the PH catalog). On the SolidWorks PC the imported PHR-2.SLDPRT / S2B-PH-K-S.SLDPRT sit in this
folder for the fit assembly; git ignores everything here except the files below.

Our own, in git: build_crimp.py builds SPH-002T_wires.SLDPRT (the two crimps and their wires, from
the dimensions in JST's catalog and the SPH-002T-P0.5S drawing) in the PHR-2 model's frame, so it takes the housing's placement.
