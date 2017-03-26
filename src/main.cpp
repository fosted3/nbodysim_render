#define CUBE 0
#define SPHERE 1
#define SHELL 2
#define LINEAR 0
#define EXP 1
#define NORMAL 2
#define FRONT 0
#define SIDE 1
#define TOP 2
#define ISO 3
#define BW 0
#define HEAT 1

#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <queue>
#include <math.h>
#include <stdio.h>
#include <fstream>
#include <cassert>
#include <pthread.h>
#include <FreeImage.h>
#include <limits>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <malloc.h>
#include <mutex>
#include "particle.h"

#ifdef DOUBLE
#ifndef datatype
#define datatype double
#endif
#endif
#ifdef FLOAT
#ifndef datatype
#define datatype float
#endif
#endif

typedef std::unordered_set<particle*> particle_set;

struct file_write_data //Used for writing data (binary, text, and image)
{
	particle_set *particles;
	unsigned int frame;
	unsigned int img_w;
	unsigned int img_h;
	unsigned int projection;
	unsigned int color;
	datatype scale;
	datatype brightness;
	bool binary;
	bool text;
	bool image;
	bool overwrite;
	bool keep_prev_binary;
	bool keep_prev_text;
	bool adaptive;
	bool nonlinear;

};

struct settings
{
	bool read_existing;			//Read data from existing binary file
	bool use_seed;				//Use user specified seed
	bool display_progress;		//Display estimated percentage completion during Barnes-Hut calculations
	bool dump_binary;			//Write binary (resume) data to file
	bool dump_text;				//Write text (position) data to plaintext
	bool dump_image;			//Write image to file
	bool overwrite_data;		//If file exists, overwrite it
	bool keep_previous_binary;	//Keep previous binary file after writing
	bool keep_previous_text;	//Keep previous text file after writing
	bool verbose;				//Display extra information
	bool damping;				//Close interaction damping
	bool adaptive_brightness;	//Adaptive brightness
	bool nonlinear_brightness;	//Nonlinear brightness
	bool collide;				//Calculate collisions
	bool cuda;				//Use CUDA
	unsigned int num_particles; //How many particles to generate if no resume present
	unsigned int num_frames;	//How many frames to compute
	unsigned int threads;		//How many threads to use for Barnes-Hut calculation
	unsigned int img_w;			//Image width
	unsigned int img_h;			//Image height
	unsigned int projection;	//Image projection
	datatype size;				//Size of cube if cubic generation specified
	datatype theta;				//Theta value for Barnes-Hut calculations
	datatype dt;				//Time between frames
	unsigned long seed;			//User specified seed value
	datatype min_mass;			//Minimum particle mass
	datatype max_mass;			//Maximum particle mass
	datatype min_vel;			//Minimum particle velocity
	datatype max_vel;			//Maximum particle velocity
	datatype r_sphere;			//Radius of sphere if sphere / shell generation specified
	datatype rotation_magnitude;//Rotation of sphere / shell
	datatype scale_x;			//Scale particle x coordinate
	datatype scale_y;			//Scale particle y coordinate
	datatype scale_z;			//Scale particle z coordinate
	datatype brightness;		//How much white to add to location on image if particle lands in pixel
	datatype scale;				//Image scale (zoom)
	datatype collision_range;	//Distance at which particles collide
	vector rotation_vector;		//Vector of rotation (use in file as rotation_vector x y z)
	unsigned int gen_type;		//How simulation generates particles (cube, sphere, shell)
	unsigned int mass_dist;		//How mass is distributed (normal, exp, linear)
	unsigned int vel_dist;		//How velocity is distrubuted in cubic mode (normal, exp, linear)
	unsigned int color;			//Color mapping
	datatype min_node_size;
};

bool file_exists(const char *filename) //Check if a file exists
{
	std::ifstream ifile(filename);
	return ifile.good();
}

std::string gen_filename(unsigned int frame, bool binary) //Generate text / binary filename
{
	std::string filename = std::to_string(frame);
	while (filename.length() < 4) { filename.insert(0, "0"); }
	filename.insert(0, "./data/");
	if (binary) { filename += ".dat"; }
	else { filename += ".txt"; }
	return filename;
}

std::string gen_image(unsigned int frame) //Generate image filename
{
	std::string filename = std::to_string(frame);
	while (filename.length() < 4) { filename.insert(0, "0"); }
	filename.insert(0, "./img/");
	filename += ".png";
	return filename;
}

datatype clamp(datatype a, datatype x, datatype b) //Make x such that a < x < b
{
	if (x < a) { return a; }
	if (x > b) { return b; }
	return x;
}

bool read_data(particle_set *particles, unsigned int frame) //Read data back into particles
{
	assert (particles -> size() == 0);
	unsigned int size = sizeof(particle);
	unsigned int num_particles;
	if (!file_exists(gen_filename(frame, true).c_str()))
	{
		return false;
	}
	std::fstream infile(gen_filename(frame, true), std::ios::in | std::ios::binary);
	particle temp;
	particle *to_add;
	infile.seekg(0, infile.end);
	num_particles = infile.tellg() / size;
	infile.seekg(0, infile.beg);
	for (unsigned int i = 0; i < num_particles; i++)
	{
		infile.read((char*) &temp, size);
		to_add = new particle(temp);
		particles -> insert(to_add);
	}
	return true;
}

void write_image(unsigned int img_w, unsigned int img_h, unsigned int projection, unsigned int color, bool adaptive, bool nonlinear, datatype scale, datatype brightness, unsigned int frame, particle_set *particles)
{
	FIBITMAP *image = FreeImage_Allocate(img_w, img_h, 24); //Image allocation, wxh, 24bpp
	RGBQUAD pixel; //Color variable
	if (!image)
	{
		std::cerr << "Can't allocate memory for image. Exiting." << std::endl;
		exit(1);
	}
	std::vector<std::vector<datatype> > temp(img_w, std::vector<datatype>(img_h)); //Temporary datatype array for more precise coloring
	for (unsigned int i = 0; i < img_w; i++)
	{
		for (unsigned int j = 0; j < img_h; j++)
		{
			temp[i][j] = 0;
		}
	}
	assert(projection == FRONT || projection == SIDE || projection == TOP || projection == ISO);
	datatype x = 0; //XY position of current particle
	datatype y = 0;
	int v = 0; //Value variable
	datatype max = 0;
	if (projection == ISO) { scale *= 2.0; } //Isometric projection shrinks scale by 2, compensate for this
	for (particle_set::iterator itr = particles -> begin(); itr != particles -> end(); itr++)
	{
		if (projection == FRONT)
		{
			x = (*itr) -> get_pos() -> get_x() + (img_w / 2);
			y = (*itr) -> get_pos() -> get_z() + (img_h / 2);
		}
		else if (projection == SIDE)
		{
			x = (*itr) -> get_pos() -> get_y() + (img_w / 2);
			y = (*itr) -> get_pos() -> get_z() + (img_h / 2);
		}
		else if (projection == TOP)
		{
			x = (*itr) -> get_pos() -> get_x() + (img_w / 2);
			y = (*itr) -> get_pos() -> get_y() + (img_h / 2);
		}
		else
		{
			x = ((sqrt(3) / 2.0) * ((*itr) -> get_pos() -> get_x() - (*itr) -> get_pos() -> get_y()) + img_w) / 2.0;
			y = ((-0.5) * ((*itr) -> get_pos() -> get_x() + (*itr) -> get_pos() -> get_y() + 2 * (*itr) -> get_pos() -> get_z()) + img_h) / 2.0;
		}
		x += (x - (img_w / 2)) * (scale - 1); //Scaling
		y += (y - (img_h / 2)) * (scale - 1);
		if (x < 0 || y < 0 || x > img_w - 1 || y > img_w - 1) { continue; }
		x = clamp(0, x, img_w - 1); //Make sure it's inside the image
		y = clamp(0, y, img_h - 1);
		if (projection == ISO) { y = (img_h - 1) - y; } //I'm not quite sure why but the image flipped upside down in isometric
		/*if (x != x || y != y)
		{
			(*itr) -> print();
			exit(1);
		}*/
		temp[(int) x][(int) y] += brightness; //Store value into array
		if (temp[(int) x][(int) y] > max) { max = temp[(int) x][(int) y]; }
	}
	if (nonlinear)
	{
		max = sqrt(max);
		for (unsigned int i = 0; i < img_w; i++)
		{
			for (unsigned int j = 0; j < img_h; j++)
			{
				temp[i][j] = sqrt(temp[i][j]);
			}
		}
	}
	for (unsigned int x_i = 0; x_i < img_w; x_i++)
	{
		for (unsigned int y_i = 0; y_i < img_h; y_i++)
		{
			if (color == BW)
			{
				if (adaptive)
				{
					v = (int) clamp(0, temp[x_i][y_i] * 255.0 / max, 255);
				}
				else
				{
					v = (int) clamp(0, temp[x_i][y_i], 255); //Cast to int and clamp to sane values
				}
				pixel.rgbRed = v;
				pixel.rgbBlue = v;
				pixel.rgbGreen = v;
				FreeImage_SetPixelColor(image, x_i, y_i, &pixel); //Set pixel
			}
			else if (color == HEAT)
			{
				if (adaptive)
				{
					v = (int) clamp(0, temp[x_i][y_i] * 511.0 / max, 511);
				}
				else
				{
					v = (int) clamp(0, temp[x_i][y_i], 511);
				}
				if (v < 256)
				{
					pixel.rgbBlue = clamp(0, 255 - v, 255);
					pixel.rgbGreen = clamp(0, v, 255);
					pixel.rgbRed = 0;
				}
				else
				{
					v -= 256;
					pixel.rgbBlue = 0;
					pixel.rgbGreen = clamp(0, 255 - v, 255);
					pixel.rgbRed = clamp(0, v, 255);
				}
				FreeImage_SetPixelColor(image, x_i, y_i, &pixel);
			}
		}
	}
	if (!FreeImage_Save(FIF_PNG, image, gen_image(frame).c_str(), 0)) //Make sure the image is saved
	{
		std::cerr << "Cannot save " << gen_image(frame) << ". Exiting." << std::endl;
		exit(1);
	}
	FreeImage_Unload(image); //Deallocate memory from image
}

void set_default(settings &s) //Set settings to default values
{
	s.read_existing = false;
	s.use_seed = false;
	s.display_progress = true;
	s.dump_binary = true;
	s.dump_text = true;
	s.dump_image = true;
	s.overwrite_data = false;
	s.keep_previous_binary = false;
	s.keep_previous_text = true;
	s.verbose = false;
	s.num_particles = 5000;
	s.num_frames = 300;
	s.size = 512;
	s.theta = 0.5;
	s.dt = 0.0333;
	s.seed = 0xDEADBEEF;
	s.min_mass = 5e10;
	s.max_mass = 5e11;
	s.min_vel = 0;
	s.max_vel = 0;
	s.r_sphere = 100;
	s.rotation_magnitude = 0.1;
	s.rotation_vector = vector(0, 0, 1);
	s.gen_type = CUBE;
	s.mass_dist = LINEAR;
	s.vel_dist = LINEAR;
	s.scale_x = 1;
	s.scale_y = 1;
	s.scale_z = 1;
	s.damping = false;
	s.threads = 1;
	s.brightness = 255;
	s.projection = ISO;
	s.img_w = 1920;
	s.img_h = 1080;
	s.scale = 1;
	s.color = BW;
	s.adaptive_brightness = false;
	s.nonlinear_brightness = false;
	s.collide = false;
	s.collision_range = 0.01;
	s.min_node_size = 0.0;
	s.cuda = false;
}

void read_settings(settings &s, const char* sfile) //Read config file
{
	std::cout << "Reading config file " << sfile << std::endl;
	std::string var;
	if (file_exists(sfile))
	{
		std::ifstream cfg(sfile);
		while (cfg)
		{
			cfg >> var;
			if (var.compare("read_existing") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.read_existing = true; }
				else { s.read_existing = false; }
			}
			else if (var.compare("use_seed") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.use_seed = true; }
				else { s.use_seed = false; }
			}
			else if (var.compare("display_progress") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.display_progress = true; }
				else { s.display_progress = false; }
			}
			else if (var.compare("dump_binary") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.dump_binary = true; }
				else { s.dump_binary = false; }
			}
			else if (var.compare("dump_text") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.dump_text = true; }
				else { s.dump_text = false; }
			}
			else if (var.compare("dump_image") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.dump_image = true; }
				else { s.dump_image = false; }
			}
			else if (var.compare("overwrite_data") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.overwrite_data = true; }
				else { s.overwrite_data = false; }
			}
			else if (var.compare("keep_previous_binary") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.keep_previous_binary = true; }
				else { s.keep_previous_binary = false; }
			}
			else if (var.compare("keep_previous_text") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.keep_previous_text = true; }
				else { s.keep_previous_text = false; }
			}
			else if (var.compare("verbose") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.verbose = true; }
				else { s.verbose = false; }
			}
			else if (var.compare("damping") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.damping = true; }
				else { s.damping = false; }
			}
			else if (var.compare("adaptive_brightness") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.adaptive_brightness = true; }
				else { s.adaptive_brightness = false; }
			}
			else if (var.compare("nonlinear_brightness") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.nonlinear_brightness = true; }
				else { s.nonlinear_brightness = false; }
			}
			else if (var.compare("collide") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.collide = true; }
				else { s.collide = false; }
			}
			else if (var.compare("cuda") == 0)
			{
				cfg >> var;
				if (var.compare("true") == 0) { s.cuda = true; }
				else { s.cuda = false; }
			}
			else if (var.compare("rotation_vector") == 0)
			{
				datatype x;
				datatype y;
				datatype z;
				cfg >> x;
				cfg >> y;
				cfg >> z;
				s.rotation_vector = vector(x, y, z);
			}
			else if (var.compare("gen_type") == 0)
			{
				cfg >> var;
				s.gen_type = 128;
				if (var.compare("cube") == 0) { s.gen_type = CUBE; }
				else if (var.compare("sphere") == 0) { s.gen_type = SPHERE; }
				else if (var.compare("shell") == 0) { s.gen_type = SHELL; }
				assert(s.gen_type != 128);
			}
			else if (var.compare("mass_dist") == 0)
			{
				cfg >> var;
				s.mass_dist = 128;
				if (var.compare("linear") == 0) { s.mass_dist = LINEAR; }
				else if (var.compare("exp") == 0) { s.mass_dist = EXP; }
				else if (var.compare("normal") == 0) { s.mass_dist = NORMAL; }
				assert(s.mass_dist != 128);
			}
			else if (var.compare("vel_dist") == 0)
			{
				cfg >> var;
				s.vel_dist = 128;
				if (var.compare("linear") == 0) { s.vel_dist = LINEAR; }
				else if (var.compare("exp") == 0) { s.vel_dist = EXP; }
				else if (var.compare("normal") == 0) { s.vel_dist = NORMAL; }
				assert(s.vel_dist != 128);
			}
			else if (var.compare("projection") == 0)
			{
				cfg >> var;
				s.projection = 128;
				if (var.compare("front") == 0) { s.projection = FRONT; }
				else if (var.compare("side") == 0) { s.projection = SIDE; }
				else if (var.compare("top") == 0) { s.projection = TOP; }
				else if (var.compare("iso") == 0) { s.projection = ISO; }
				assert(s.projection != 128);
			}
			else if (var.compare("color") == 0)
			{
				cfg >> var;
				s.color = 128;
				if (var.compare("bw") == 0) { s.color = BW; }
				else if (var.compare("heat") == 0) { s.color = HEAT; }
				assert(s.color != 128);
			}
			else if (var.compare("threads") == 0) { cfg >> s.threads; }
			else if (var.compare("scale_x") == 0) { cfg >> s.scale_x; }
			else if (var.compare("scale_y") == 0) { cfg >> s.scale_y; }
			else if (var.compare("scale_z") == 0) { cfg >> s.scale_z; }
			else if (var.compare("r_sphere") == 0) { cfg >> s.r_sphere; }
			else if (var.compare("rotation_magnitude") == 0) { cfg >> s.rotation_magnitude; }
			else if (var.compare("num_particles") == 0) { cfg >> s.num_particles; }
			else if (var.compare("num_frames") == 0) { cfg >> s.num_frames; }
			else if (var.compare("size") == 0) { cfg >> s.size; }
			else if (var.compare("theta") == 0) { cfg >> s.theta; }
			else if (var.compare("dt") == 0) { cfg >> s.dt; }
			else if (var.compare("seed") == 0) { cfg >> s.seed; }
			else if (var.compare("min_mass") == 0) { cfg >> s.min_mass; }
			else if (var.compare("max_mass") == 0) { cfg >> s.max_mass; }
			else if (var.compare("min_vel") == 0) { cfg >> s.min_vel; }
			else if (var.compare("max_vel") == 0) { cfg >> s.max_vel; }
			else if (var.compare("brightness") == 0) { cfg >> s.brightness; }
			else if (var.compare("img_w") == 0) { cfg >> s.img_w; }
			else if (var.compare("img_h") == 0) { cfg >> s.img_h; }
			else if (var.compare("scale") == 0) { cfg >> s.scale; }
			else if (var.compare("collision_range") == 0) { cfg >> s.collision_range; }
			else if (var.compare("min_node_size") == 0) { cfg >> s.min_node_size; }
			else if (var.compare("false") == 0) { continue; }
			else if (var.compare("true") == 0) { continue; }
			else { std::cerr << "Unrecognized variable " << var << std::endl; }
		}
	}
	else
	{
		std::cerr << sfile << " doesn't exist or can't be read." << std::endl;
		exit(1);
	}
}

int main(int argc, char **argv)
{
	settings config;
	set_default(config);
	particle_set *particles = NULL;
	if (argc == 1)
	{
		read_settings(config, "settings.cfg");
	}
	else if (argc == 2)
	{
		read_settings(config, argv[1]);
	}
	else
	{
		std::cerr << "Usage: " << argv[0] << " [settings file]" << std::endl;
		exit(1);
	}
	for (unsigned int frame = 0; frame < config.num_frames; frame++)
	{
		particles = new particle_set;
		if (read_data(particles, frame))
		{
			write_image(config.img_w, config.img_h, config.projection, config.color, config.adaptive_brightness, config.nonlinear_brightness, config.scale, config.brightness, frame, particles);
		}
		else
		{
			break;
		}
		delete particles;
		particles = NULL;
	}
	if (particles != NULL) { 	delete particles; }
	return 0;
}
