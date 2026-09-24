import argparse
from .classification import classify_model
from .parser import parse_problem_file
from .presolve import presolve


def main(argv=None):
    parser = argparse.ArgumentParser(description="Sovereign mathematical optimization model classifier (V1)")
    parser.add_argument("--input", required=True, help="Path to one optimization problem file (content, not extension, is parsed)")
    parser.add_argument("--presolve", action="store_true", help="Run safe presolve reductions after classification")
    args = parser.parse_args(argv)
    try:
        model = parse_problem_file(args.input)
        classification = classify_model(model)
        print("=" * 60 + "\nOPTIMIZATION PROBLEM ANALYSIS\n" + "=" * 60)
        print("\n" + model.render())
        print("\nDetected problem type: " + classification.problem_type)
        print("Reason: " + classification.reason)
        if args.presolve:
            result = presolve(model)
            print("\n" + "=" * 60 + "\nPRESOLVE\n" + "=" * 60)
            print(f"Original variables:       {len(model.variables)}")
            print(f"Original constraints:     {len(model.constraints)}")
            for label, value in (("Bound tightenings", result.stats.bound_tightenings), ("Variables fixed", result.stats.variables_fixed), ("Variables eliminated", result.stats.variables_eliminated), ("Redundant rows removed", result.stats.redundant_rows_removed), ("Aggregations", result.stats.aggregations), ("Substitutions", result.stats.substitutions), ("Singleton reductions", result.stats.singleton_reductions)):
                print(f"{label + ':':26}{value:>3}")
            print(f"Final variables:          {len(result.model.variables)}")
            print(f"Final constraints:        {len(result.model.constraints)}")
            print(f"Status:                   {result.status}")
            print(f"Presolve passes:          {result.passes}")
            for message in result.messages: print("Message: " + message)
            print("=" * 60)
        print("=" * 60)
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
