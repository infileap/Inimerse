try:
    1 / 0
except Exception as e:
    print(type(e).__name__ + " | " + str(e))
