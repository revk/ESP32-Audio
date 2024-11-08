// Generated case design for LED/LED.kicad_pcb
// By https://github.com/revk/PCBCase
// Generated 2024-09-16 09:02:32
// title:	PCB-LED
// rev:	1
// company:	Adrian Kennard, Andrews & Arnold Ltd
//

// Globals
margin=0.200000;
lip=2.000000;
casebottom=2.000000;
casetop=7.000000;
casewall=3.000000;
fit=0.000000;
edge=2.000000;
pcbthickness=1.600000;
nohull=false;
hullcap=1.000000;
hulledge=1.000000;
useredge=false;

module outline(h=pcbthickness,r=0){linear_extrude(height=h)offset(r=r)polygon(points=[[0.000089,2.977084],[0.000000,13.090000],[0.031947,13.355609],[0.125968,13.606066],[0.276698,13.827084],[0.475541,14.006055],[0.711151,14.132770],[0.970089,14.200000],[14.250089,16.000000],[42.000089,16.000000],[42.390270,15.961570],[42.765456,15.847758],[43.111230,15.662939],[43.414303,15.414213],[43.663029,15.111140],[43.847849,14.765367],[43.961660,14.390181],[44.000089,14.000000],[44.000089,2.000000],[43.961660,1.609819],[43.847848,1.234633],[43.663029,0.888859],[43.414303,0.585786],[43.111230,0.337060],[42.765456,0.152241],[42.390270,0.038429],[42.000089,0.000000],[14.250089,0.000000],[1.033663,1.794444],[0.757780,1.866073],[0.506751,2.001080],[0.294896,2.191763],[0.134302,2.427246],[0.034128,2.694093]],paths=[[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33]]);}

module pcb(h=pcbthickness,r=0){linear_extrude(height=h)offset(r=r)polygon(points=[[0.000089,2.977084],[0.000000,13.090000],[0.031947,13.355609],[0.125968,13.606066],[0.276698,13.827084],[0.475541,14.006055],[0.711151,14.132770],[0.970089,14.200000],[14.250089,16.000000],[42.000089,16.000000],[42.390270,15.961570],[42.765456,15.847758],[43.111230,15.662939],[43.414303,15.414213],[43.663029,15.111140],[43.847849,14.765367],[43.961660,14.390181],[44.000089,14.000000],[44.000089,2.000000],[43.961660,1.609819],[43.847848,1.234633],[43.663029,0.888859],[43.414303,0.585786],[43.111230,0.337060],[42.765456,0.152241],[42.390270,0.038429],[42.000089,0.000000],[14.250089,0.000000],[1.033663,1.794444],[0.757780,1.866073],[0.506751,2.001080],[0.294896,2.191763],[0.134302,2.427246],[0.034128,2.694093]],paths=[[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33]]);}
spacing=60.000089;
pcbwidth=44.000089;
pcblength=16.000000;
// Parts to go on PCB (top)
module parts_top(part=false,hole=false,block=false){
translate([13.600089,9.975000,1.600000])rotate([0,0,-90.000000])m0(part,hole,block,casetop); // D2 (back)
translate([32.900089,8.915000,1.600000])rotate([-0.000000,-0.000000,-90.000000])m1(part,hole,block,casetop); // RevK:L_4x4_ TYA4020 (back)
translate([31.100089,15.415000,1.600000])m2(part,hole,block,casetop); // RevK:R_0402_ R_0402_1005Metric (back)
translate([32.900089,11.515000,1.600000])m3(part,hole,block,casetop); // RevK:C_0603_ C_0603_1608Metric (back)
translate([23.500089,10.000000,1.600000])m4(part,hole,block,casetop); // REF** (back)
translate([35.400089,5.300000,1.600000])rotate([0,0,-90.000000])m3(part,hole,block,casetop); // RevK:C_0603_ C_0603_1608Metric (back)
translate([10.900089,14.900000,1.600000])rotate([0,0,180.000000])m2(part,hole,block,casetop); // RevK:R_0402_ R_0402_1005Metric (back)
translate([33.350089,2.700000,1.600000])rotate([0,0,-90.000000])m5(part,hole,block,casetop); // C1 (back)
translate([40.100089,1.100000,1.600000])rotate([0,0,-90.000000])m6(part,hole,block,casetop); // RevK:C_0402 C_0402_1005Metric (back)
translate([34.700090,15.415000,1.600000])m2(part,hole,block,casetop); // RevK:R_0402_ R_0402_1005Metric (back)
translate([12.725089,1.250000,1.600000])m3(part,hole,block,casetop); // RevK:C_0603_ C_0603_1608Metric (back)
translate([39.110089,8.000000,1.600000])rotate([0,0,90.000000])translate([0.000000,-2.400000,0.000000])rotate([90.000000,-0.000000,-0.000000])m7(part,hole,block,casetop); // RevK:USB-C-Socket-H CSP-USC16-TR (back)
translate([40.874089,14.287500,1.600000])rotate([0,0,90.000000])m8(part,hole,block,casetop); // T1 (back)
translate([32.900089,15.415000,1.600000])m2(part,hole,block,casetop); // RevK:R_0402_ R_0402_1005Metric (back)
translate([41.797727,1.600000,1.600000])rotate([0,0,-135.000000])m9(part,hole,block,casetop); // D1 (back)
translate([9.100089,14.700000,1.600000])rotate([0,0,180.000000])m6(part,hole,block,casetop); // RevK:C_0402 C_0402_1005Metric (back)
translate([32.900089,13.515000,1.600000])m10(part,hole,block,casetop); // RevK:SOT-23-6-MD8942 SOT-23-6 (back)
translate([31.150089,6.100000,1.600000])rotate([0,0,180.000000])m3(part,hole,block,casetop); // RevK:C_0603_ C_0603_1608Metric (back)
translate([38.800089,1.100000,1.600000])rotate([0,0,-90.000000])m2(part,hole,block,casetop); // RevK:R_0402_ R_0402_1005Metric (back)
translate([30.400089,12.215000,1.600000])rotate([0,0,90.000000])m3(part,hole,block,casetop); // RevK:C_0603_ C_0603_1608Metric (back)
translate([33.396834,6.020564,1.600000])m6(part,hole,block,casetop); // RevK:C_0402 C_0402_1005Metric (back)
translate([37.900089,1.100000,1.600000])rotate([0,0,90.000000])m2(part,hole,block,casetop); // RevK:R_0402_ R_0402_1005Metric (back)
translate([13.000089,15.000000,1.600000])m3(part,hole,block,casetop); // RevK:C_0603_ C_0603_1608Metric (back)
translate([35.400089,12.215000,1.600000])rotate([0,0,90.000000])m3(part,hole,block,casetop); // RevK:C_0603_ C_0603_1608Metric (back)
translate([21.950089,8.000000,1.600000])m11(part,hole,block,casetop); // U6 (back)
translate([7.000089,8.000000,1.600000])m12(part,hole,block,casetop); // J2 (back)
}

parts_top=7;
// Parts to go on PCB (bottom)
module parts_bottom(part=false,hole=false,block=false){
}

parts_bottom=0;
module b(cx,cy,z,w,l,h){translate([cx-w/2,cy-l/2,z])cube([w,l,h]);}
module m0(part=false,hole=false,block=false,height)
{ // D2
// DFN1006-2L
if(part)
{
	b(0,0,0,1.0,0.6,0.45); // Chip
}
}

module m1(part=false,hole=false,block=false,height)
{ // RevK:L_4x4_ TYA4020
// 4x4 Inductor
if(part)
{
	b(0,0,0,4,4,3);
}
}

module m2(part=false,hole=false,block=false,height)
{ // RevK:R_0402_ R_0402_1005Metric
// 0402 Resistor
if(part)
{
	b(0,0,0,1.5,0.65,0.2); // Pad size
	b(0,0,0,1.0,0.5,0.5); // Chip
}
}

module m3(part=false,hole=false,block=false,height)
{ // RevK:C_0603_ C_0603_1608Metric
// 0603 Capacitor
if(part)
{
	b(0,0,0,1.6,0.8,1); // Chip
	b(0,0,0,1.6,0.95,0.2); // Pad size
}
}

module m4(part=false,hole=false,block=false,height)
{ // REF**
// Nothing
}

module m5(part=false,hole=false,block=false,height)
{ // C1
if(part)
{
	b(0,0,0,3.5,2.8,1.9);
}
}

module m6(part=false,hole=false,block=false,height)
{ // RevK:C_0402 C_0402_1005Metric
// 0402 Capacitor
if(part)
{
	b(0,0,0,1.0,0.5,1); // Chip
	b(0,0,0,1.5,0.65,0.2); // Pad size
}
}

module m7(part=false,hole=false,block=false,height)
{ // RevK:USB-C-Socket-H CSP-USC16-TR
// USB connector
rotate([-90,0,0])translate([-4.47,-3.84,0])
{
	if(part)
	{
		b(4.47,7,0,7,2,0.2);	// Pads
		translate([1.63,-0.2,1.63])
		rotate([-90,0,0])
		hull()
		{
			cylinder(d=3.26,h=7.55,$fn=24);
			translate([5.68,0,0])
			cylinder(d=3.26,h=7.55,$fn=24);
		}
		translate([0,6.2501,0])cube([8.94,1.1,1.6301]);
		translate([0,1.7,0])cube([8.94,1.6,1.6301]);
	}
	if(hole)
	{
		// Plug
		translate([1.63,-20,1.63])
		rotate([-90,0,0])
		hull()
		{
			cylinder(d=2.5,h=21,$fn=24);
			translate([5.68,0,0])
			cylinder(d=2.5,h=21,$fn=24);
		}
		translate([1.63,-22.5,1.63])
		rotate([-90,0,0])
		hull()
		{
			cylinder(d=7,h=21,$fn=24);
			translate([5.68,0,0])
			cylinder(d=7,h=21,$fn=24);
		}
	}
}
}

module m8(part=false,hole=false,block=false,height)
{ // T1
if(part)
{
	b(0,0,0,2.65,3.5,0.98);
}
if(hole)
{
	translate([0,-0.75,-height-0.1])
		cylinder(d1=2,d2=1,h=height);
}
}

module m9(part=false,hole=false,block=false,height)
{ // D1
// 1x1mm LED
if(part)
{
        b(0,0,0,1.2,1.2,.8);
}
if(hole)
{
        hull()
        {
                b(0,0,.8,1.2,1.2,1);
                translate([0,0,height])cylinder(d=2,h=1,$fn=16);
        }
}
if(block)
{
        hull()
        {
                b(0,0,0,2.4,2.4,1);
                translate([0,0,height])cylinder(d=4,h=1,$fn=16);
        }
}
}

module m10(part=false,hole=false,block=false,height)
{ // RevK:SOT-23-6-MD8942 SOT-23-6
// SOT-23-6
if(part)
{
	b(0,0,0,1.726,3.026,1.2); // Part
	b(0,0,0,3.6,2.5,0.5); // Pins
}
}

module m11(part=false,hole=false,block=false,height)
{ // U6
// ESP32-S3-MINI-1
translate([-15.4/2,-15.45/2,0])
{
	if(part)
	{
		cube([15.4,20.5,0.8]);
		translate([0.7,0.5,0])cube([14,13.55,2.4]);
	}
	if(hole)
	{
		cube([15.4,20.5,0.8]);
	}
}
}

module m12(part=false,hole=false,block=false,height)
{ // J2
// WAGO-2060-453-998-404
N=3;
if(part)
{
	translate([-0.9,0,0])hull()
	{
		b(0,0,0,12.7,N*4-0.1,1);
		translate([0.8,0,0])b(0,0,0,11.1,N*4-0.1,4.5);
	}
}
if(hole)
	for(p=[0:N-1])translate([0,-4*(N-1)/2+p*4,2])rotate([90,0,-90])cylinder(d=3,h=20);
}

// Generate PCB casework

height=casebottom+pcbthickness+casetop;
$fn=48;

module pyramid()
{ // A pyramid
 polyhedron(points=[[0,0,0],[-height,-height,height],[-height,height,height],[height,height,height],[height,-height,height]],faces=[[0,1,2],[0,2,3],[0,3,4],[0,4,1],[4,3,2,1]]);
}


module pcb_hulled(h=pcbthickness,r=0)
{ // PCB shape for case
	if(useredge)outline(h,r);
	else hull()outline(h,r);
}

module solid_case(d=0)
{ // The case wall
	hull()
        {
                translate([0,0,-casebottom])pcb_hulled(height,casewall-edge);
                translate([0,0,edge-casebottom])pcb_hulled(height-edge*2,casewall);
        }
}

module preview()
{
	pcb();
	color("#0f0")parts_top(part=true);
	color("#0f0")parts_bottom(part=true);
	color("#f00")parts_top(hole=true);
	color("#f00")parts_bottom(hole=true);
	color("#00f8")parts_top(block=true);
	color("#00f8")parts_bottom(block=true);
}

module top_half(step=false)
{
	difference()
	{
		translate([-casebottom-100,-casewall-100,pcbthickness-lip/2+0.01]) cube([pcbwidth+casewall*2+200,pcblength+casewall*2+200,height]);
		if(step)translate([0,0,pcbthickness-lip/2-0.01])pcb_hulled(lip,casewall/2+fit);
	}
}

module bottom_half(step=false)
{
	translate([-casebottom-100,-casewall-100,pcbthickness+lip/2-height-0.01]) cube([pcbwidth+casewall*2+200,pcblength+casewall*2+200,height]);
	if(step)translate([0,0,pcbthickness-lip/2])pcb_hulled(lip,casewall/2-fit);
}

module case_wall()
{
	difference()
	{
		solid_case();
		translate([0,0,-height])pcb_hulled(height*2);
	}
}

module top_side_hole()
{
	intersection()
	{
		parts_top(hole=true);
		case_wall();
	}
}

module bottom_side_hole()
{
	intersection()
	{
		parts_bottom(hole=true);
		case_wall();
	}
}

module parts_space()
{
	minkowski()
	{
		union()
		{
			parts_top(part=true,hole=true);
			parts_bottom(part=true,hole=true);
		}
		sphere(r=margin,$fn=6);
	}
}

module top_cut()
{
	difference()
	{
		top_half(true);
		if(parts_top)difference()
		{
			minkowski()
			{ // Penetrating side holes
				top_side_hole();
				rotate([180,0,0])
				pyramid();
			}
			minkowski()
			{
				top_side_hole();
				rotate([0,0,45])cylinder(r=margin,h=height,$fn=4);
			}
		}
	}
	if(parts_bottom)difference()
	{
		minkowski()
		{ // Penetrating side holes
			bottom_side_hole();
			pyramid();
		}
			minkowski()
			{
				bottom_side_hole();
				rotate([0,0,45])translate([0,0,-height])cylinder(r=margin,h=height,$fn=4);
			}
	}
}

module bottom_cut()
{
	difference()
	{
		 translate([-casebottom-50,-casewall-50,-height]) cube([pcbwidth+casewall*2+100,pcblength+casewall*2+100,height*2]);
		 top_cut();
	}
}

module top_body()
{
	difference()
	{
		intersection()
		{
			solid_case();
			pcb_hulled(height);
			top_half();
		}
		if(parts_top)minkowski()
		{
			if(nohull)parts_top(part=true);
			else hull()parts_top(part=true);
			translate([0,0,margin-height])cylinder(r=margin,h=height,$fn=8);
		}
	}
	intersection()
	{
		solid_case();
		parts_top(block=true);
	}
}

module top_edge()
{
	intersection()
	{
		case_wall();
		top_cut();
	}
}

module top()
{
	translate([casewall,casewall+pcblength,pcbthickness+casetop])rotate([180,0,0])difference()
	{
		union()
		{
			top_body();
			top_edge();
		}
		parts_space();
		translate([0,0,pcbthickness-height])pcb(height,r=margin);
	}
}

module bottom_body()
{
	difference()
	{
		intersection()
		{
			solid_case();
			translate([0,0,-height])pcb_hulled(height);
			bottom_half();
		}
		if(parts_bottom)minkowski()
		{
			if(nohull)parts_bottom(part=true);
			else hull()parts_bottom(part=true);
			translate([0,0,-margin])cylinder(r=margin,h=height,$fn=8);
		}
	}
	intersection()
	{
		solid_case();
		parts_bottom(block=true);
	}
}

module bottom_edge()
{
	intersection()
	{
		case_wall();
		bottom_cut();
	}
}

module bottom()
{
	translate([casewall,casewall,casebottom])difference()
	{
		union()
		{
        		bottom_body();
        		bottom_edge();
		}
		parts_space();
		pcb(height,r=margin);
	}
}
bottom(); translate([spacing,0,0])top();
