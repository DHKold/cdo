# cdo

Portable C/C++ project managed by CDo.

## First Run

```powershell
. .\.cdo\activate.ps1
cdo doctor
cdo target create app --type executable
cdo build app
cdo run app
cdo test
```

## Adding Targets

```powershell
cdo target create mylib --type library --static
cdo target create game --type executable
cdo target list
```

## Dependencies

Add catalog dependencies with:

```powershell
cdo add sdl3
```