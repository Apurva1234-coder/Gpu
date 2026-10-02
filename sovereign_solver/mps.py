"""Parser for the standard (free or fixed-column) MPS linear format."""
from .validation import validate_payload


def parse_mps(text: str):
    lines = [line.rstrip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("*")]
    if not lines or lines[0].split()[0].upper() != "NAME":
        raise ValueError("MPS input must begin with a NAME section.")
    section = None
    name = lines[0].split(maxsplit=1)[1].strip() if len(lines[0].split()) > 1 else "MPS problem"
    rows, columns, row_coefficients, rhs, ranges, bounds = {}, {}, {}, {}, {}, {}
    objective_row = None
    integer_vars = set()
    binary_vars = set()
    in_integer_block = False
    rhs_set = ranges_set = bounds_set = None
    for raw in lines[1:]:
        tokens = raw.split()
        head = tokens[0].upper()
        if len(tokens) == 1 and head in {"NAME", "ROWS", "COLUMNS", "RHS", "BOUNDS", "RANGES", "ENDATA", "OBJSENSE", "QSECTION"}:
            section = head
            if head == "ENDATA":
                break
            if head == "QSECTION":
                raise ValueError("Quadratic MPS sections are not supported in V1.")
            continue
        if section == "ROWS":
            if len(tokens) < 2 or tokens[0].upper() not in {"N", "E", "L", "G"}:
                raise ValueError(f"Invalid MPS ROWS record: {raw.strip()}")
            row_type, row_name = tokens[0].upper(), tokens[1]
            rows[row_name] = row_type
            if row_type == "N" and objective_row is None:
                objective_row = row_name
        elif section == "COLUMNS":
            if len(tokens) >= 3 and tokens[1].upper() == "'MARKER'":
                in_integer_block = "INTORG" in tokens[2].upper()
                continue
            if len(tokens) < 3 or (len(tokens) - 1) % 2:
                raise ValueError(f"Invalid MPS COLUMNS record: {raw.strip()}")
            variable = tokens[0]
            if in_integer_block:
                integer_vars.add(variable)
            for index in range(1, len(tokens), 2):
                row, value = tokens[index], _number(tokens[index + 1], raw)
                if row not in rows:
                    raise ValueError(f"MPS column references unknown row '{row}'.")
                columns.setdefault(variable, {})[row] = columns.setdefault(variable, {}).get(row, 0) + value
                row_coefficients.setdefault(row, {})[variable] = columns[variable][row]
        elif section == "RHS":
            if len(tokens) < 3:
                raise ValueError(f"Invalid MPS RHS record: {raw.strip()}")
            rhs_set = rhs_set or tokens[0]
            if tokens[0] == rhs_set:
                _read_pairs(tokens, rhs, raw)
        elif section == "RANGES":
            if len(tokens) < 3:
                raise ValueError(f"Invalid MPS RANGES record: {raw.strip()}")
            ranges_set = ranges_set or tokens[0]
            if tokens[0] == ranges_set:
                _read_pairs(tokens, ranges, raw)
        elif section == "BOUNDS":
            if len(tokens) < 3:
                raise ValueError(f"Invalid MPS BOUNDS record: {raw.strip()}")
            bounds_set = bounds_set or tokens[1]
            if tokens[1] != bounds_set:
                continue
            bound_type, variable = tokens[0].upper(), tokens[2]
            value = _number(tokens[3], raw) if len(tokens) > 3 else None
            lower, upper = bounds.get(variable, (0.0, None))
            if bound_type == "LO": lower = value
            elif bound_type in {"LI", "UI"}:
                integer_vars.add(variable)
                if bound_type == "LI": lower = value
                else: upper = value
            elif bound_type == "UP": upper = value
            elif bound_type == "FX": lower = upper = value
            elif bound_type in {"FR", "MI"}: lower = None
            elif bound_type == "PL": upper = None
            elif bound_type == "BV":
                lower, upper = 0.0, 1.0
                integer_vars.add(variable)
                binary_vars.add(variable)
            else: raise ValueError(f"Unsupported MPS bound type '{bound_type}'.")
            bounds[variable] = (lower, upper)
        elif section == "OBJSENSE":
            # Supported common extension: OBJSENSE followed by MAX/MIN.
            pass
        else:
            raise ValueError(f"MPS record appears before a recognized section: {raw.strip()}")
    if section != "ENDATA" or not rows or objective_row is None or not columns:
        raise ValueError("Incomplete MPS file: NAME, ROWS, COLUMNS, and ENDATA are required.")
    sense = "minimize"
    for raw in lines:
        if raw.strip().upper() in {"MAX", "MAXIMIZE"}: sense = "maximize"
        if raw.strip().upper() in {"MIN", "MINIMIZE"}: sense = "minimize"
    variable_names = list(columns)
    data = {"name": name, "objective_sense": sense, "variables": [], "objective": {}, "constraints": [], "bounds": {}}
    for variable in variable_names:
        variable_type = "binary" if variable in binary_vars else "integer" if variable in integer_vars else "continuous"
        data["variables"].append({"name": variable, "type": variable_type})
        if variable in bounds:
            data["bounds"][variable] = list(bounds[variable])
        for row, coefficient in columns[variable].items():
            if row == objective_row:
                data["objective"][variable] = coefficient
    for row, row_type in rows.items():
        if row_type == "N": continue
        operator = {"E": "=", "L": "<=", "G": ">="}[row_type]
        coefficients = dict(row_coefficients.get(row, {}))
        row_rhs = rhs.get(row, 0.0)
        if row not in ranges:
            data["constraints"].append({"name": row, "coefficients": coefficients, "operator": operator, "rhs": row_rhs})
            continue
        width = abs(ranges[row])
        if operator == "L":
            lower, upper = row_rhs, row_rhs + width
        elif operator == "G":
            lower, upper = row_rhs - width, row_rhs
        elif ranges[row] >= 0:
            lower, upper = row_rhs, row_rhs + width
        else:
            lower, upper = row_rhs + ranges[row], row_rhs
        data["constraints"].append({"name": row + "_RANGE_LO", "coefficients": coefficients, "operator": ">=", "rhs": lower})
        data["constraints"].append({"name": row + "_RANGE_UP", "coefficients": coefficients, "operator": "<=", "rhs": upper})
    return validate_payload(data)


def _read_pairs(tokens, target, raw):
    if len(tokens) < 3 or (len(tokens) - 1) % 2:
        raise ValueError(f"Invalid MPS RHS record: {raw.strip()}")
    for index in range(1, len(tokens), 2):
        target[tokens[index]] = _number(tokens[index + 1], raw)


def _number(value, raw):
    try:
        return float(value.replace("D", "E").replace("d", "e"))
    except ValueError as exc:
        raise ValueError(f"Invalid numeric value '{value}' in MPS record: {raw.strip()}") from exc
