# ffmpeg360
ffmpeg360 is a tool we created for converting 360-degree images and videos between various projection schemes including equirectangular, standard cube, offset cube, equi-angular cube, and our proposed MiniView layout. 

Given any supported input projection schemes, it can also render views in rectilinear projection. 

# How to compile
## Mac instructions
```
brew install glfw glew pkg-config yasm
./configure --enable-opengl --enable-libx264 --enable-gpl --extra-libs='-framework OpenGL -lglfw -lGLEW -lpng -lm -lz'
make ffmpeg
```
## Ubuntu instructions
```
./configure --enable-opengl --extra-libs='-lGL -lGLU -lGLEW -lglfw -lpng -lm -lz'
make ffmpeg
```

# Files
```
    libavfilter/vf_project.c
    libavfilter/gl_utils.h
    libavfilter/gl_utils.c
```
vertex and fragment shader files for various input and output projections:
```
    ffmpeg360_shader/eqdeg.glsl
    ffmpeg360_shader/eqdis-ecoef.glsl
    ffmpeg360_shader/eqdis.glsl
    ffmpeg360_shader/equirectangular-eac.glsl
    ffmpeg360_shader/equirectangular.glsl
    ffmpeg360_shader/simpleVertex.glsl
    ffmpeg360_shader/uneqdeg-ecoef.glsl
    ffmpeg360_shader/uneqdeg.glsl
    ffmpeg360_shader/vertex.glsl
```

# Layout file
Both input layout and output layout are specified by the `.lt` files. The following `.lt` files are included:
```
    ffmpeg360_layout/baseball.lt
    ffmpeg360_layout/cube.lt
    ffmpeg360_layout/equirectangular.lt
    ffmpeg360_layout/example.lt
    ffmpeg360_layout/good.lt
    ffmpeg360_layout/good_normal.lt
    ffmpeg360_layout/rotated_cube.lt
    ffmpeg360_layout/vcube.lt
```
Each line in the `.lt` file contains the following information:
```
{w}:{h}:{x-fov}:{y-fov}:{x-rotation}:{y-rotation}:{z-rotation}:{u}:{v}
```
Here, `w`, `h`are the normalized width and height of this tile on input frame

`u`, `v` are the normalized coordinates of the left upper corner of this tile, while the origin is the left upper corner of the whole picture

`x-fov`, `y-fov`, `x-rotation`, `y-rotation`, and `z-rotation` are represented in degrees

An example:

```
0.333333:0.5:90:90:0:0:0:0.333333:0
```

To create these `.lt` files, we can first create a human-friendly layout file in the following format:
```
${tile_width}:${tile_height}:${vertical_fov_degree}:${rotation_by_x}:${rotation_by_y}:${rotation_by_z}:${sampling_method}:${x_on_output}:${y_on_output}
```
For example, consider the following cube layout:
```
+--------+--------+--------+
| right  | left   | top    |
|        |        |        |
+--------+--------+--------+
| bottom | front  | back   |
|        |        |        |
+--------+--------+--------+
```
The corresponding human-friendly layout file is as follows:
```
1800:1200
600:600:90:0:90:0:eqdis:0:0
600:600:90:0:270:0:eqdis:600:0
600:600:90:90:0:0:eqdis:1200:0
600:600:90:-90:0:0:eqdis:0:600
600:600:90:0:0:0:eqdis:600:600
600:600:90:0:180:0:eqdis:1200:600
```
We can then use a tool, `normalize.pl` to convert the human-friendly layout file to the ffmpeg360-compatible file, e.g.,
```
$ ./normalize.pl good.lt > good_normal.lt
```

Layout file for other layouts are also pre-defined:

### Equirectangular layout

The layout describing the equirectangular layout is defined in ```equirectangular.lt```

**Note** that the equirectangular layout is special as it only has one "tile". Here is an example use:
```
./ffmpeg -loglevel "info" -y -i smallRapeseed.jpg -filter:v "project=512:512:90:90:0:0:0:simpleVertex.glsl:equirectangular.glsl::equirectangular.lt" view.jpg
```

### Baseball layout

The layout describing baseball cube layout is defined in ```baseball.lt```

### Standard cube layout

The layout describing standard cube layout is defined in ```cube.lt```

### MiniView layout

The _normalized_ MiniView layout is dfiend in ```good_normal.lt```

# How to use the projection filter

Parameters to the projection filter are passed as follows:

```
projection={output width}:{output height}:{x-fov}:{y-fov}:{x-rotation}:{y-rotation}:{z-rotation}:{vertex shader}:{fragment shader}:{orientation file}:{layout file}:{start time}:{expand coefficient}
```

Here, ```output width``` and ```output height``` are represented in pixels
 
```x-fov```, ```y-fov```, ```x-rotation```, ```y-rotation``` and ```z-rotation``` are represented in degrees. 
The legal ranges of ```x/y/z-rotation``` are set to {-360, 360}.

```vertex shader```, ```fragment shader```, ```orientation file``` and ```layout file``` are file paths

```start time``` indicates where to start loading orinetation time

For example, a sample command can look like the follows:

```
$ ./ffmpeg -loglevel 'info' -y -i baseball.mp4 -filter:v "project=800:800:90:90:0:180:0:vertex.glsl:eqdis.glsl::baseball.lt:0.0:1.0" baseball-back.mp4
```

In the options, ```orientation file``` and ```layout file``` can be omitted. In this case, the orientation will be fixed at the start direction given by ```x/y/z-rotation``` and the default layout will be the standard cube layout, which can be find in ```cube.lt``` file. ```expand coefficient``` is 1.0 by default.

More examples:
```
$ ./ffmpeg -loglevel 'info' -y -i cube.mp4 -filter:v "project=800:800:90:90:0:180:0:vertex.glsl:eqdis.glsl::cube.lt:0.0:1.0" back.mp4
$ ./ffmpeg -loglevel 'info' -y -i eac.mp4 -filter:v "project=800:800:90:90:0:180:0:vertex.glsl:uneqdeg.glsl::cube.lt:0.0:1.0" back.mp4
```

## Expand coefficient

### Ouput frames with expand coefficient
The ```{expand coefficient}``` parameter in the projection filter parameters is something special. With it set to a value greater than 1, we are encoding more pixels than the FOV. As a result, the new fov is calculated as:
```
new-fov = atan( tan(old-fov / 2) * ecoef ) * 2
```
The new fov is bigger than the old fov.

### Input frames with expand coefficient

```eqdis.glsl```, ```eqdeg.glsl```, and ```uneqdeg.glsl``` support sampling without expand coefficient.
```eqdis-ecoef.glsl``` and ```uneqdeg-ecoef.glsl``` suport default expand coeffient of ```1.01```.

# remap.pl

```remap.pl``` is a perl script that overlays multiple tiles onto one single frame. For example, the project filter only outputs MiniViews but not the final MiniView layout. To overlay all 82 MiniViews that cover the entire sphere, ```remap.pl``` calls 82 filters that creates these MiniViews, then uses ffmpeg's overlay filter to place them onto a single frame. 

Similarly, the project filter can output equi-angular faces or standard cube faces but not the final EAC cubemap. ```remap.pl``` calls six project filter instances, creates these "filtered" frames, and overlay them on the output frame.

Usage:
```
./remap.pl $option=value
```
```
options (default values):
  iv: input video
  ov: output video (out.mp4)
  il: input layout (cube.lt)
  ol: output layout
  res: output width and height, use like res=1280x1080
  ofs: output fragment shader (eqdis.glsl)
  ovs: output vertex shader (vertex.glsl)
  dflag: debug flag (info)
  crf: constant rate factor - 0 is lossless compression
  ecoef: the expand ecoefficient
  cbr: constant bitrate
```
For example, to convert equirectangular to cube:
```
./remap.pl iv=equi.mp4 ov=cube.mp4 res=3000x2000 il=equirectangular.lt ofs=equirectangular.glsl ovs=simpleVertex.glsl ol=cube.lt crf=18 ecoef=1.01
```
To convert equirectangular to EAC:
```
./remap.pl iv=equi.mp4 ov=eac.mp4 res=3000x2000 il=equirectangular.lt ofs=equirectangular-eac.glsl ovs=simpleVertex.glsl ol=cube.lt crf=18 ecoef=1.01
```
To convert equirectangular to MiniView:
```
./remap.pl iv=equi.mp4 ov=miniview.mp4 res=2240x832 il=equirectangular.lt ofs=equirectangular.glsl ovs=simpleVertex.glsl ol=good_normal.lt crf=18 ecoef=1.01
```
To convert EAC to cube:
```
./remap.pl iv=eac.mp4 ov=eac-cube.mp4 res=3000x2000 il=cube.lt ofs=uneqdeg-ecoef.glsl ovs=vertex.glsl ol=cube.lt crf=18 ecoef=1.01
```

# Limitation

Currently, ffmpeg360 does not support converting arbitrary projection to the equirectangular project.
Converting from standard cube to EAC works, but not with non-1.0 input expand coefficient. That is, if cube has non-1.0 expand coefficient, the generated EAC may not be correct. 
