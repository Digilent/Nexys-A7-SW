# Nexys A7 Software Repository

This repository contains Vitis workspaces for all software demos for the Nexys A7.

For more information about the Nexys A7, visit its [Resource Center](https://reference.digilentinc.com/reference/programmable-logic/nexys-a7/start) on the Digilent Wiki.

Each demo contained in this repository is documented on the Digilent Wiki, links in the table below.

| Name and Wiki Link | Description |
|--------------------|-------------|
| [Nexys A7 DMA Audio Demo](https://reference.digilentinc.com/reference/programmable-logic/nexys-a7/demos/dma-audio) | This project demonstrates how to stream data in and out of the Nexys A7's RAM |

## Repository Description

This repository contains the Vitis workspace and software sources for all of the software demos that we provide for the Nexys A7. As each of these demos also requires a hardware design contained in a Vivado project, this repository should not be used directly. The [Nexys A7](https://github.com/Digilent/Nexys-A7) repository contains all sources for these demos across all tools, and pulls in all of this repository's sources by using it as a submodule.

For instructions on how to use this repository with git, and for additional documentation on the submodule and branch structures used, please visit [Digilent FPGA Demo Git Repositories](https://reference.digilentinc.com/reference/programmable-logic/documents/git) on the Digilent Wiki. Note that use of git is not required to use this demo. Digilent recommends the use of project releases, for which instructions can be found in each demo wiki page, linked in the table of demos, above.

Demos were moved into this repository during 2020.1 updates. History of these demos prior to these updates can be found in their old repositories, linked below:
* https://github.com/Digilent/Nexys-A7-50T-DMA-Audio
* https://github.com/Digilent/Nexys-A7-100T-DMA-Audio